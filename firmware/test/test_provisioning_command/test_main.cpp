#include <unity.h>
#include "../../src/ProvisioningCommand.h"
#include <cstring>

using namespace stopwatch;

void test_parseSetCommands(void) {
    ProvisioningCommand cmd;
    TEST_ASSERT_TRUE(parseProvisioningCommand("wifi ssid HomeNet", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetWiFiSSID, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("HomeNet", cmd.value);

    TEST_ASSERT_TRUE(parseProvisioningCommand("wifi password pass phrase", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetWiFiPassword, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("pass phrase", cmd.value);

    TEST_ASSERT_TRUE(parseProvisioningCommand("api base-url https://stopwatch.example.com", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetAPIBaseURL, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("https://stopwatch.example.com", cmd.value);

    TEST_ASSERT_TRUE(parseProvisioningCommand("api cf-client-id client-id", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetCFAccessClientID, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("client-id", cmd.value);

    TEST_ASSERT_TRUE(parseProvisioningCommand("api cf-client-secret client-secret", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetCFAccessClientSecret, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("client-secret", cmd.value);

    TEST_ASSERT_TRUE(parseProvisioningCommand("api token abc123", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetAPIToken, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("abc123", cmd.value);
}

void test_parseShowAndClear(void) {
    ProvisioningCommand cmd;
    TEST_ASSERT_TRUE(parseProvisioningCommand("config show", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::ShowConfig, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("", cmd.value);

    TEST_ASSERT_TRUE(parseProvisioningCommand("config clear", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::ClearConfig, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("", cmd.value);
}

void test_parseDownloadModeTrigger(void) {
    ProvisioningCommand cmd;
    TEST_ASSERT_TRUE(parseProvisioningCommand("STOPWATCH-DL", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::EnterDownloadMode, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("", cmd.value);
}

void test_rejectsUnknownOrEmptyCommands(void) {
    ProvisioningCommand cmd;
    TEST_ASSERT_FALSE(parseProvisioningCommand("", cmd));
    TEST_ASSERT_FALSE(parseProvisioningCommand(nullptr, cmd));
    TEST_ASSERT_FALSE(parseProvisioningCommand("wifi", cmd));
    TEST_ASSERT_FALSE(parseProvisioningCommand("wifi ssid ", cmd));
    TEST_ASSERT_FALSE(parseProvisioningCommand("api nope x", cmd));
    TEST_ASSERT_FALSE(parseProvisioningCommand("config dump", cmd));
}

void test_failedParseLeavesOutputUnchanged(void) {
    ProvisioningCommand cmd;
    TEST_ASSERT_TRUE(parseProvisioningCommand("api token previous-secret", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetAPIToken, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("previous-secret", cmd.value);

    TEST_ASSERT_FALSE(parseProvisioningCommand("wifi ssid ", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetAPIToken, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("previous-secret", cmd.value);

    TEST_ASSERT_FALSE(parseProvisioningCommand("", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetAPIToken, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("previous-secret", cmd.value);

    TEST_ASSERT_FALSE(parseProvisioningCommand(nullptr, cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetAPIToken, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("previous-secret", cmd.value);

    TEST_ASSERT_FALSE(parseProvisioningCommand("config dump", cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetAPIToken, (int)cmd.action);
    TEST_ASSERT_EQUAL_STRING("previous-secret", cmd.value);
}

void test_longValuesAreTruncatedAndTerminated(void) {
    char line[kProvisioningValueMax + 32] = {};
    const char *prefix = "api token ";
    size_t offset = 0;
    while (prefix[offset] != '\0') {
        line[offset] = prefix[offset];
        ++offset;
    }
    for (size_t i = offset; i < sizeof(line) - 1; ++i) {
        line[i] = 'a';
    }

    ProvisioningCommand cmd;
    TEST_ASSERT_TRUE(parseProvisioningCommand(line, cmd));
    TEST_ASSERT_EQUAL((int)ProvisioningAction::SetAPIToken, (int)cmd.action);
    TEST_ASSERT_EQUAL_CHAR('\0', cmd.value[kProvisioningValueMax - 1]);
    TEST_ASSERT_EQUAL_UINT(kProvisioningValueMax - 1, strlen(cmd.value));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_parseSetCommands);
    RUN_TEST(test_parseShowAndClear);
    RUN_TEST(test_parseDownloadModeTrigger);
    RUN_TEST(test_rejectsUnknownOrEmptyCommands);
    RUN_TEST(test_failedParseLeavesOutputUnchanged);
    RUN_TEST(test_longValuesAreTruncatedAndTerminated);
    return UNITY_END();
}
