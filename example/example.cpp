#include <thread>
#include <gsl/gsl_multifit.h>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <unordered_map>
#include <set>
#include <string>
#include <limits>

#include <calcgraph.h>

enum TradeSignal { BUY, SELL, HOLD };
static const char *TradeSignalNames[] = {"BUY", "SELL", "HOLD"};

/**
 * @brief An order on an exchange; the position the trading bot wants to take.
 */
struct Order final {
    const uint8_t ticker;
    const TradeSignal type;
    const double price;

    Order(uint8_t ticker, TradeSignal type, double price)
        : ticker(ticker), type(type), price(price) {
        printf("opening %s @ %0.3f on %dY\n", TradeSignalNames[type], price,
               ticker);
    }

    void close(double current_price) {
        double pnl;
        if (type == BUY) {
            pnl = current_price - price;
        } else {
            pnl = price - current_price;
        }

        printf("closing %s @ %0.3f on %dY at %0.3f, P&L %0.3f\n",
               TradeSignalNames[type], price, ticker, current_price, pnl);
    }
};

using uint8_vector = std::shared_ptr<std::vector<uint8_t>>;
using double_vector = std::shared_ptr<std::vector<double>>;
using uint8double_vector =
    std::shared_ptr<std::vector<std::pair<uint8_t, double>>>;
using string = std::shared_ptr<std::string>;
using strings = std::shared_ptr<std::forward_list<string>>;
using order = std::shared_ptr<Order>;

/**
 * @brief Polyfit quadratic functions
 */
static const uint8_t DEGREE = 3;

/**
 * @brief Port to listen on
 */
static const short PORT = 8080;

/**
 * @brief Global termination flag, so we can set it in signal handlers
 */
static std::atomic<bool> stop(false);

/**
 * @brief Size of the UDP datagram buffer
 */
static const int buffer_len = 4096;

/**
 * The "benchmark" maturities, or instruments we'll consider when building (via
 * polyfit) the yield curve.
 */
static std::set<uint8_t> BENCHMARKS = {1, 5, 10};

static calcgraph::Graph g;

/**
 * @brief The distance from the interpolated yield curve a price must be to
 * trigger a "buy" or "sell" signal
 */
static const double THRESHOLD = 0.1;

/**
 * @brief Fit a polynomial curve to 2-dimensional data
 * @see http://rosettacode.org/wiki/Polynomial_regression#C
 */
double_vector polyfit(const uint8_vector dx, const double_vector dy) {

    // can't fit NaN prices
    if (!dx->size() || dx->size() != dy->size() ||
        std::any_of(dy->begin(), dy->end(),
                    [](double p) { return std::isnan(p); })) {
        return double_vector(); // not enough pieces
    }

    double chisq;
    auto X = gsl_matrix_alloc(dx->size(), DEGREE);
    auto y = gsl_vector_alloc(dx->size());
    auto c = gsl_vector_alloc(DEGREE);
    auto cov = gsl_matrix_alloc(DEGREE, DEGREE);

    for (int i = 0; i < dx->size(); i++) {
        for (int j = 0; j < DEGREE; j++) {
            gsl_matrix_set(X, i, j, pow(dx->at(i), j));
        }
        gsl_vector_set(y, i, dy->at(i));
    }

    auto ws = gsl_multifit_linear_alloc(dx->size(), DEGREE);
    gsl_multifit_linear(X, y, c, cov, &chisq, ws);

    double_vector out = double_vector(new std::vector<double>());
    for (int i = 0; i < DEGREE; i++) {
        out->push_back(gsl_vector_get(c, i));
    }

    gsl_multifit_linear_free(ws);
    gsl_matrix_free(X);
    gsl_matrix_free(cov);
    gsl_vector_free(y);
    gsl_vector_free(c);

    return out;
}

void build_pipeline(uint8_t ticker, calcgraph::Connectable<double> &price,
                    calcgraph::Connectable<double_vector> *curve,
                    double initial_price) {
    auto signal_generator =
        g.node()
            .propagate<calcgraph::OnChange>()
            .latest(&price, initial_price)
            .latest(curve)
            .connect([ticker](double price, double_vector yield_curve) {

                if (!yield_curve || std::isnan(price)) {
                    return HOLD; // not initialized properly
                }

                // work out the model price ("fair value") from our fitted yield
                // curve
                double fair_value = 0.0;
                for (uint8_t i = 0; i < DEGREE; ++i) {
                    fair_value += pow(ticker, i) * yield_curve->at(i);
                }

                // if the market price deviates from the model price by more
                // than a THRESHOLD amount, generate a trading signal
                if (price > fair_value + THRESHOLD)
                    return SELL;
                else if (price < fair_value - THRESHOLD)
                    return BUY;
                else
                    return HOLD;
            });

    auto order_manager =
        g.node()
            .propagate<calcgraph::Weak>() // so we don't wake ourselves up
            .latest(&price, initial_price)
            .latest(signal_generator.get(), HOLD)
            .unconnected<order>()
            .connect([ticker](double price, TradeSignal sig, order current) {
                switch (sig) {
                case HOLD:
                    return current;
                case BUY:
                case SELL:
                    if (current) {
                        if (current->type == sig) {
                            return current; // keep holding
                        } else {
                            current->close(price);
                        }
                    }
                    return order(new Order(ticker, sig, price));
                }
            });
    order_manager->connect(order_manager->input<2>());
}

/**
 * @brief Parse the given quotes into maturity-yield pairs.
 */
uint8double_vector dispatch(strings msgs) {
    uint8double_vector ret =
        uint8double_vector(new uint8double_vector::element_type());
    for (auto msg : *msgs) {
        uint8_t ticker = std::stoi(*msg);
        double price = std::stod(msg->substr(msg->find(" ") + 1));
        ret->emplace_back(ticker, price);
    }
    return ret;
}

/**
 * @brief Set up a UDP socket and pass any (complete) received datagrams to the
 * Input.
 * @returns true iff the listening process started correctly
 */
bool listen_to_datagrams(calcgraph::Input<string> &&in) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        perror("socket");
        return false;
    }
    int oval = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &oval, sizeof(oval)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        return false;
    }

    // set up a timeout so we check the "stop" flag once a second (to break
    // out
    // of the receive loop)
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        return false;
    }
    struct sockaddr_in myaddr = {.sin_family = AF_INET,
                                 .sin_addr = htonl(INADDR_ANY),
                                 .sin_port = htons(PORT)};
    if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        perror("bind");
        return false;
    }

    char buffer[buffer_len];
    struct iovec iov = {.iov_base = buffer, .iov_len = buffer_len};
    struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1};
    int byterecv;
    while (!stop.load()) {
        if ((byterecv = recvmsg(fd, &msg, 0)) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                errno == EINPROGRESS || errno == EINTR) {
                continue; // probably timeout
            } else {
                perror("recvmsg");
                return false;
            }
        } else if (msg.msg_flags & MSG_TRUNC) {
            continue; // skip broken packets
        } else {
            in.append(g, string(new std::string(buffer, byterecv)));
        }
    }
    return true;
}

void install_sigint_handler() {
    auto handler = [](int sig) {
        stop.store(true);
        std::cerr << "received signal " << sig << ", exiting" << std::endl;
    };
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGHUP, handler);
}

int main() {
    install_sigint_handler();

    std::thread t(calcgraph::evaluate_repeatedly, std::ref(g), std::ref(stop));

    auto dispatcher =
        g.node()
            .propagate<calcgraph::OnChange>()
            .output<calcgraph::MultiValued<calcgraph::Demultiplexed>::type>()
            .accumulate(calcgraph::unconnected<string>())
            .connect(dispatch);

    auto curve_fitter = g.node()
                            .propagate<calcgraph::OnChange>()
                            .variadic<uint8_t>()
                            .variadic<double>()
                            .connect(polyfit);

    for (uint8_t benchmark : BENCHMARKS) {
        curve_fitter->variadic_add<0>(benchmark);
        auto price = dispatcher->keyed_output(benchmark);
        price.connect(curve_fitter->variadic_add<1>(NAN));
        build_pipeline(benchmark, price, curve_fitter.get(), NAN);
    }

    dispatcher->embed([&curve_fitter](auto new_pair, auto &output) {
        auto price = output.keyed_output(new_pair->first);
        build_pipeline(new_pair->first, *price, curve_fitter.get(),
                       new_pair->second);
    });

    if (!listen_to_datagrams(dispatcher->input<0>())) {
        stop.store(true);
    }

    t.join();
    return 0;
}