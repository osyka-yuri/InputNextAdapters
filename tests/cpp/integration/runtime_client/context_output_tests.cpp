#include "support.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>

#include <catch2/catch_test_macros.hpp>

namespace inputnext::test::runtime_client {

TEST_CASE("runtime_client.context_outputs_follow_status_contract",
    "[integration][runtime_client]") {
    const auto& fixture = get_fixtures()[0];
    with_connected_client(fixture, [&](RuntimeClient& client) {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        input_next_effects_open_v1 request{};
        request.struct_size = sizeof(request);
        request.target.struct_size = sizeof(request.target);
        request.target.runtime_id[0] = 41;
        input_next_effects_context_v1 context{};
        context.struct_size = sizeof(context);
        context.id = 91;
        for (const auto status :
            {INPUT_NEXT_STATUS_PENDING, INPUT_NEXT_STATUS_REVOKED, INPUT_NEXT_STATUS_BUSY}) {
            observer.state().open_status = status;
            const auto preserved = context;
            REQUIRE(client.open_context(request, context) == status);
            REQUIRE(same_context(context, preserved));
        }
        observer.state().open_status = INPUT_NEXT_STATUS_OK;
        REQUIRE(client.open_context(request, context) == INPUT_NEXT_STATUS_OK);
        REQUIRE(context.runtime_id[0] == 41);
        REQUIRE(context.id != 91);

        input_next_effects_context_info_v1 info{};
        info.struct_size = sizeof(info);
        info.context = context;
        info.state = 97;
        for (const auto status : {INPUT_NEXT_STATUS_PENDING, INPUT_NEXT_STATUS_BUSY}) {
            observer.state().query_context_status = status;
            const auto preserved = info;
            REQUIRE(client.query_context(context, info) == status);
            REQUIRE(info.state == preserved.state);
            REQUIRE(same_context(info.context, preserved.context));
        }
        observer.state().query_context_status = INPUT_NEXT_STATUS_REVOKED;
        REQUIRE(client.query_context(context, info) == INPUT_NEXT_STATUS_REVOKED);
        REQUIRE(info.state == INPUT_NEXT_EFFECTS_CONTEXT_REVOKED);
        REQUIRE(same_context(info.context, context));
    });
}

} // namespace inputnext::test::runtime_client
