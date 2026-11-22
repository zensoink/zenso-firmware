#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <algorithm>
#include <sstream>
#include <iomanip>

struct OtaDirective {
  std::string version;
  std::string url;
  std::string checksum_sha256;
  size_t size_bytes = 0;
};

// Seam under test: format bytes to hex string
std::string ota_bytes_to_hex(const uint8_t *bytes, size_t len) {
  std::ostringstream oss;
  for (size_t i = 0; i < len; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
  }
  return oss.str();
}

// Seam under test: validate checksum case-insensitively
bool ota_validate_checksum(const std::string &expected_hex, const uint8_t *digest_bytes) {
  if (expected_hex.length() != 64) {
    return false;
  }
  std::string actual_hex = ota_bytes_to_hex(digest_bytes, 32);
  if (expected_hex.length() != actual_hex.length()) {
    return false;
  }
  for (size_t i = 0; i < 64; ++i) {
    if (std::tolower(expected_hex[i]) != std::tolower(actual_hex[i])) {
      return false;
    }
  }
  return true;
}

void test_bytes_to_hex() {
  uint8_t bytes[4] = {0xde, 0xad, 0xbe, 0xef};
  std::string hex = ota_bytes_to_hex(bytes, 4);
  assert(hex == "deadbeef");
  std::cout << "[PASS] test_bytes_to_hex" << std::endl;
}

void test_checksum_validation_exact_match() {
  uint8_t digest[32];
  for (int i = 0; i < 32; ++i) digest[i] = (uint8_t)i;
  std::string hex = ota_bytes_to_hex(digest, 32);

  assert(ota_validate_checksum(hex, digest));
  std::cout << "[PASS] test_checksum_validation_exact_match" << std::endl;
}

void test_checksum_validation_case_insensitive() {
  uint8_t digest[32];
  for (int i = 0; i < 32; ++i) digest[i] = (uint8_t)(0x10 + i);
  std::string hex_upper = ota_bytes_to_hex(digest, 32);
  std::transform(hex_upper.begin(), hex_upper.end(), hex_upper.begin(), ::toupper);

  assert(ota_validate_checksum(hex_upper, digest));
  std::cout << "[PASS] test_checksum_validation_case_insensitive" << std::endl;
}

void test_checksum_validation_mismatch() {
  uint8_t digest[32] = {0};
  uint8_t wrong_digest[32] = {0};
  wrong_digest[31] = 1;
  std::string hex = ota_bytes_to_hex(digest, 32);

  assert(!ota_validate_checksum(hex, wrong_digest));
  assert(!ota_validate_checksum("invalid_short_hash", digest));
  std::cout << "[PASS] test_checksum_validation_mismatch" << std::endl;
}

int main() {
  std::cout << "Running OTA Unit Tests..." << std::endl;
  test_bytes_to_hex();
  test_checksum_validation_exact_match();
  test_checksum_validation_case_insensitive();
  test_checksum_validation_mismatch();
  std::cout << "All OTA unit tests passed!" << std::endl;
  return 0;
}
