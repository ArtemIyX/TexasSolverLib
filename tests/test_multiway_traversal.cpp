#include "solver/multiway_traversal.hpp"
#include "test_harness.hpp"

TEST_CASE(multiway_traversal_appends_external_sampling_worker_deltas) {
    core::MultiwayWorkerDeltaStream stream(0, 4);
    core::MultiwayExternalSamplingRequest request;
    request.player_reaches = {1.0, 0.5, 0.25};
    request.traverser = 1;
    request.chance_reach = 1.0;
    request.sampling_reach = 0.25;
    request.traverser_reach = 0.5;
    request.strategy = {0.25, 0.75};
    request.sampled_action_values = {2.0, 4.0};

    EXPECT_TRUE(core::MultiwayExternalSamplingTraversal::append_infoset_update(
        stream, {{17}, 1}, 3, 99, request));
    EXPECT_EQ(stream.size(), 2U);
    EXPECT_EQ(stream.deltas()[0].trajectory_id, 99U);
    EXPECT_EQ(stream.deltas()[1].action, 1U);
}

TEST_CASE(multiway_traversal_rejects_an_insufficient_delta_stream) {
    core::MultiwayWorkerDeltaStream stream(0, 1);
    core::MultiwayExternalSamplingRequest request;
    request.player_reaches = {1.0, 1.0};
    request.traverser = 0;
    request.strategy = {0.5, 0.5};
    request.sampled_action_values = {0.0, 1.0};
    EXPECT_TRUE(!core::MultiwayExternalSamplingTraversal::append_infoset_update(
        stream, {{17}, 0}, 0, 1, request));
    EXPECT_EQ(stream.size(), 0U);
}
