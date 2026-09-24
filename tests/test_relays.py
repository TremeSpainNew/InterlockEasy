"""Host regression test: python3 tests/test_relays.py (requires c++)."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUBS = {
    "Arduino.h": """#pragma once
#include <cstdint>
#include <string>
using String = std::string;
using byte = uint8_t;
constexpr int INPUT = 0;
constexpr int INPUT_PULLUP = 2;
inline void pinMode(int, int) {}
inline int digitalRead(int) { return 0; }
inline unsigned long millis() { return 0; }
struct SerialStub {
    void println(const char*) {}
    template<class... T> void printf(const char*, T...) {}
};
inline SerialStub Serial;
""",
    "ArduinoJson.h": "#pragma once\nclass JsonDocument {}; class JsonVariantConst {};\n",
    "Preferences.h": "#pragma once\nclass Preferences {};\n",
    "PubSubClient.h": "#pragma once\nclass PubSubClient {};\n",
    "Wire.h": """#pragma once
#include <cassert>
#include <vector>
struct WireStub {
    bool beginOk = true;
    int failAt = -1;
    std::vector<std::vector<uint8_t>> writes;
    bool begin(int sda, int scl, int hz) {
        assert(sda == 42 && scl == 41 && hz == 100000);
        return beginOk;
    }
    void setTimeOut(int) {}
    void beginTransmission(uint8_t address) {
        assert(address == 0x20);
        writes.emplace_back();
    }
    void write(uint8_t value) { writes.back().push_back(value); }
    uint8_t endTransmission() { return int(writes.size()) == failAt ? 2 : 0; }
};
inline WireStub Wire;
""",
}
TEST = """#include "IOManager.h"
#include "ConfigManager.h"
#include "MqttManager.h"
#include <Wire.h>
#include <utility>
ConfigManager Config;
bool ConfigManager::relayAssigned(uint8_t) const { return false; }
MqttManager MQTT;
std::vector<std::pair<int, bool>> published;
void MqttManager::publishInput(uint8_t, bool) {}
void MqttManager::publishOutput(uint8_t channel, bool state) {
    published.emplace_back(channel, state);
}
void reset() { Wire = WireStub{}; published.clear(); IO = IOManager{}; }
int main() {
    reset(); IO.begin();
    assert((Wire.writes == std::vector<std::vector<uint8_t>>{{1, 0}, {3, 0}}));
    for (int i = 0; i < 8; ++i) {
        IO.setOutput(i, true);
        assert(IO.getOutput(i));
        assert(Wire.writes.back()[1] == (1U << (i + 1)) - 1);
    }
    IO.setOutput(3, false);
    assert(Wire.writes.back()[1] == 0xf7); // Other seven channels preserved.
    const auto count = Wire.writes.size();
    IO.setOutput(3, false); IO.setOutput(8, true); IO.setOutput(255, true);
    assert(Wire.writes.size() == count);
    const auto events = published.size();
    Wire.failAt = int(count + 1);
    IO.setOutput(0, false);
    assert(IO.getOutput(0) && published.size() == events);
    Wire.failAt = -1;
    IO.setOutput(0, false); // Failed command can be retried.
    assert(!IO.getOutput(0) && Wire.writes.back()[1] == 0xf6);
    assert(published.size() == events + 1);
    IO.setOutput(7, false);
    assert(IO.setOutputs(0x0f, 0x05));
    assert(Wire.writes.back()[1] == 0x75);
    const auto batch = Wire.writes.size();
    Wire.failAt = int(batch + 1);
    assert(!IO.setOutputs(0x0f, 0x0a));
    assert(IO.getOutput(0) && !IO.getOutput(1));
    for (int fail = 1; fail <= 2; ++fail) {
        reset(); Wire.failAt = fail; IO.begin();
        assert(Wire.writes.size() == unsigned(fail));
        IO.setOutput(0, true);
        assert(Wire.writes.size() == unsigned(fail));
        assert(!IO.getOutput(0) && published.empty());
    }
    reset(); Wire.beginOk = false; IO.begin(); IO.setOutput(0, true);
    assert(Wire.writes.empty() && published.empty());
}
"""

with tempfile.TemporaryDirectory(prefix="interlock-relays-") as directory:
    folder = Path(directory)
    for name, content in STUBS.items():
        (folder / name).write_text(content)
    (folder / "test.cpp").write_text(TEST)
    executable = folder / "test"
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        f"-I{folder}", f"-I{ROOT / 'src'}",
        str(ROOT / "src/IOManager.cpp"), str(folder / "test.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("OK: initialization, 8 channels, isolation, invalid channels and I2C failures")
