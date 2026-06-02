#include <unity.h>
#include "../../src/SettingsCodec.h"

using namespace stopwatch;

void test_roundTripsVersion3Settings(void) {
    CarouselSettings in = CarouselSettings::defaults();
    in.uprightEnabled = true;
    in.autoplayEnabled = false;
    in.intervalSeconds = 30;
    in.motionMode = CarouselMotionMode::Instant;
    in.resumeSeconds = 10;
    in.transportMode = TransportMode::WiFi;

    uint8_t bytes[kSettingsBytesSize];
    size_t len = 0;
    TEST_ASSERT_TRUE(encodeCarouselSettings(in, bytes, sizeof(bytes), len));
    TEST_ASSERT_EQUAL_UINT(kSettingsV3BytesSize, len);
    TEST_ASSERT_EQUAL_UINT8(3, bytes[0]);

    CarouselSettings out;
    TEST_ASSERT_TRUE(decodeCarouselSettings(bytes, len, out));
    TEST_ASSERT_TRUE(out.uprightEnabled);
    TEST_ASSERT_FALSE(out.autoplayEnabled);
    TEST_ASSERT_EQUAL_UINT16(30, out.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Instant, (int)out.motionMode);
    TEST_ASSERT_EQUAL_UINT16(10, out.resumeSeconds);
    TEST_ASSERT_EQUAL((int)TransportMode::WiFi, (int)out.transportMode);
}

void test_decodesVersion1WithUprightOff(void) {
    uint8_t bytes[kSettingsV2BytesSize] = {
        1, 1, (uint8_t)CarouselMotionMode::Fade, 0,
        15, 0,
        30, 0,
    };

    CarouselSettings out;
    TEST_ASSERT_TRUE(decodeCarouselSettings(bytes, sizeof(bytes), out));
    TEST_ASSERT_FALSE(out.uprightEnabled);
    TEST_ASSERT_TRUE(out.autoplayEnabled);
    TEST_ASSERT_EQUAL_UINT16(15, out.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Fade, (int)out.motionMode);
    TEST_ASSERT_EQUAL_UINT16(30, out.resumeSeconds);
    TEST_ASSERT_EQUAL((int)TransportMode::BLE, (int)out.transportMode);
}

void test_decodesVersion2TransportAsBLE(void) {
    uint8_t bytes[kSettingsV2BytesSize] = {
        2, 0x03, (uint8_t)CarouselMotionMode::Iris, 0,
        10, 0,
        20, 0,
    };

    CarouselSettings out;
    TEST_ASSERT_TRUE(decodeCarouselSettings(bytes, sizeof(bytes), out));
    TEST_ASSERT_TRUE(out.uprightEnabled);
    TEST_ASSERT_TRUE(out.autoplayEnabled);
    TEST_ASSERT_EQUAL((int)TransportMode::BLE, (int)out.transportMode);
}

void test_roundTripsVersion3TransportMode(void) {
    CarouselSettings in = CarouselSettings::defaults();
    in.transportMode = TransportMode::WiFi;

    uint8_t bytes[kSettingsMaxBytesSize];
    size_t len = 0;
    TEST_ASSERT_TRUE(encodeCarouselSettings(in, bytes, sizeof(bytes), len));
    TEST_ASSERT_EQUAL_UINT8(3, bytes[0]);
    TEST_ASSERT_EQUAL_UINT(kSettingsV3BytesSize, len);

    CarouselSettings out;
    TEST_ASSERT_TRUE(decodeCarouselSettings(bytes, len, out));
    TEST_ASSERT_EQUAL((int)TransportMode::WiFi, (int)out.transportMode);
}

void test_rejectsWrongSizeAndUnknownVersion(void) {
    CarouselSettings out;
    uint8_t shortBytes[3] = {2, 0, 0};
    TEST_ASSERT_FALSE(decodeCarouselSettings(shortBytes, sizeof(shortBytes), out));

    uint8_t unknown[kSettingsV2BytesSize] = {99, 0, 0, 0, 10, 0, 20, 0};
    TEST_ASSERT_FALSE(decodeCarouselSettings(unknown, sizeof(unknown), out));
}

void test_decodeClampsInvalidStoredValues(void) {
    uint8_t bytes[kSettingsV3BytesSize] = {
        3, 0x03, 99, 99,
        7, 0,
        11, 0,
        0, 0,
    };

    CarouselSettings out;
    TEST_ASSERT_TRUE(decodeCarouselSettings(bytes, sizeof(bytes), out));
    TEST_ASSERT_TRUE(out.uprightEnabled);
    TEST_ASSERT_TRUE(out.autoplayEnabled);
    TEST_ASSERT_EQUAL_UINT16(10, out.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)out.motionMode);
    TEST_ASSERT_EQUAL_UINT16(20, out.resumeSeconds);
    TEST_ASSERT_EQUAL((int)TransportMode::BLE, (int)out.transportMode);
}

void test_encodeFailsWhenBufferTooSmall(void) {
    CarouselSettings in = CarouselSettings::defaults();
    uint8_t bytes[kSettingsV3BytesSize - 1];
    size_t len = 123;

    TEST_ASSERT_FALSE(encodeCarouselSettings(in, bytes, sizeof(bytes), len));
    TEST_ASSERT_EQUAL_UINT(0, len);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_roundTripsVersion3Settings);
    RUN_TEST(test_decodesVersion1WithUprightOff);
    RUN_TEST(test_decodesVersion2TransportAsBLE);
    RUN_TEST(test_roundTripsVersion3TransportMode);
    RUN_TEST(test_rejectsWrongSizeAndUnknownVersion);
    RUN_TEST(test_decodeClampsInvalidStoredValues);
    RUN_TEST(test_encodeFailsWhenBufferTooSmall);
    return UNITY_END();
}
