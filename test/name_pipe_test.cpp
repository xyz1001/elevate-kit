#include "elevate_kit/name_pipe.h"

#include "elevate_kit/platform.h"

#include "doctest/doctest.h"

#include <chrono>
#include <cstdint>
#include <string>

TEST_CASE("Named pipe supports local connection and message exchange") {
    const std::string application_id =
            "eknp" + std::to_string(elevate_kit::GetPid());
    const std::string probe_session = "p";
    const std::string session_id = "r";

    auto probe =
            elevate_kit::INamedPipe::Create(application_id, probe_session);
    REQUIRE(probe);
    CHECK_FALSE(probe->Accept(std::chrono::seconds(0)));
    probe.reset();

#ifndef _WIN32
    // Windows Open waits forever for a missing named pipe, so this contract
    // cannot be tested there without introducing a thread that may block.
    auto missing = elevate_kit::INamedPipe::Open(application_id, "m");
    CHECK_FALSE(missing);
#endif

    auto server = elevate_kit::INamedPipe::Create(application_id, session_id);
    REQUIRE(server);
    auto client = elevate_kit::INamedPipe::Open(application_id, session_id);
    REQUIRE(client);

    CHECK(server->Accept(std::chrono::seconds(0)));
    CHECK(server->GetRemoteProcessId() == elevate_kit::GetPid());
    CHECK(client->GetRemoteProcessId() == elevate_kit::GetPid());

    CHECK(server->SendMessage("server-to-client"));
    const auto client_payload = client->RecvMessage();
    REQUIRE(client_payload.has_value());
    CHECK(*client_payload == "server-to-client");

    CHECK(client->SendMessage(""));
    const auto server_payload = server->RecvMessage();
    REQUIRE(server_payload.has_value());
    CHECK(server_payload->empty());
}
