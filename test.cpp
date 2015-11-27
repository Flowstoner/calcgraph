#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>
#include "calc.h"

class GraphTest final : public CppUnit::TestFixture  {
public:

    void testSingleNode() {
        struct calc::Stats stats;
        calc::Graph g;
        std::atomic<int> res(0);
        
        // setup
        auto node = g.node(
            std::plus<int>(),
            calc::unconnected<int>(),
            calc::unconnected<int>());
        node->input<0>().append(g, 1);
        node->input<1>().append(g, 2);
        node->connect(calc::Input<int>(res));
        
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 1);
        CPPUNIT_ASSERT(stats.worked == 1);
        CPPUNIT_ASSERT(res.load() == 3);

        // check an empty run
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 0);
        CPPUNIT_ASSERT(stats.worked == 0);

        // update an input
        node->input<0>().append(g, 3);
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 1);
        CPPUNIT_ASSERT(stats.worked == 1);
        CPPUNIT_ASSERT(res.load() == 5);
    }

    void testConstant() {
        struct calc::Stats stats;
        calc::Graph g;
        std::atomic<int> res;

        calc::Constant<int> one(1), two(2);
        auto node = g.node(
            std::plus<int>(),
            &one,
            &two);
        node->connect(calc::Input<int>(res));
        
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 1);
        CPPUNIT_ASSERT(stats.worked == 1);
        CPPUNIT_ASSERT(res.load() == 3);

        // check an empty run
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 0);
        CPPUNIT_ASSERT(stats.worked == 0);
    }

    void testCircular() {
        struct calc::Stats stats;
        calc::Graph g;
        std::atomic<int> res(0);
        
        // setup: connect output to second input
        auto node = g.node(
            std::plus<int>(),
            calc::unconnected<int>(),
            calc::unconnected<int>());
        node->input<0>().append(g, 1);
        node->connect(node->input<1>());
        node->connect(calc::Input<int>(res));
        
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 1);
        CPPUNIT_ASSERT(stats.worked == 1);
        CPPUNIT_ASSERT(res.load() == 1);

        // should recycle input
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 1);
        CPPUNIT_ASSERT(stats.worked == 1);
        CPPUNIT_ASSERT(res.load() == 2);

        // should recycle input again
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 1);
        CPPUNIT_ASSERT(stats.worked == 1);
        CPPUNIT_ASSERT(res.load() == 3);

        // try updating the seed
        node->input<0>().append(g, 5);
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 1);
        CPPUNIT_ASSERT(stats.worked == 1);
        CPPUNIT_ASSERT(res.load() == 8);

        // should recycle re-seeded input
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 1);
        CPPUNIT_ASSERT(stats.worked == 1);
        CPPUNIT_ASSERT(res.load() == 9);
    }

    void testChain() {
        struct calc::Stats stats;
        calc::Graph g;
        std::atomic<bool> res;
        
        // setup
        auto in1 = g.node(
            [](int a) { return a; },
            calc::unconnected<int>());
        auto in2 = g.node(
            [](int a) { return a; },
            calc::unconnected<int>());
        auto out = g.node(
            std::less<int>(),
            in1.get(),
            in2.get());
        out->connect(calc::Input<bool>(res));
        
        in1->input<0>().append(g, 1);
        in2->input<0>().append(g, 2);
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 3);
        CPPUNIT_ASSERT(stats.worked == 3);
        CPPUNIT_ASSERT(res.load() == true);

        // check an empty run
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 0);
        CPPUNIT_ASSERT(stats.worked == 0);

        // update an input & check only one runs
        in1->input<0>().append(g, 3);
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 1);
        CPPUNIT_ASSERT(stats.worked == 2);
        CPPUNIT_ASSERT(res.load() == false);

        // check an empty run
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 0);
        CPPUNIT_ASSERT(stats.worked == 0);

        // update both inputs
        in1->input<0>().append(g, 5);
        in2->input<0>().append(g, 6);
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 2);
        CPPUNIT_ASSERT(stats.worked == 3);
        CPPUNIT_ASSERT(res.load() == true);

        // check an empty run
        g(&stats);
        CPPUNIT_ASSERT(stats.queued == 0);
        CPPUNIT_ASSERT(stats.worked == 0);
    }

    CPPUNIT_TEST_SUITE(GraphTest);
    CPPUNIT_TEST(testSingleNode);
    CPPUNIT_TEST(testConstant);
    CPPUNIT_TEST(testChain);
    CPPUNIT_TEST_SUITE_END();
};

int main() {
    CppUnit::TextUi::TestRunner runner;
    runner.addTest( GraphTest::suite() );
    return !runner.run("", false);
}
