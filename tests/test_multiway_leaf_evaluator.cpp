#include "solver/multiway/continuation/multiway_leaf_evaluator.hpp"
#include "test_harness.hpp"

namespace {

texas::Value evaluate(const texas::MultiwayLeafEvaluationRequest& request, const void* context) noexcept {
    return request.traverser + *static_cast<const int*>(context);
}

}  // namespace

TEST_CASE(multiway_leaf_evaluator_uses_nonowning_function_pointer_context) {
    const int offset = 7;
    const texas::MultiwayLeafEvaluator evaluator = {evaluate, &offset};
    EXPECT_TRUE(evaluator.valid());
    EXPECT_EQ(evaluator({nullptr, nullptr, 2}), 9.0);
}
