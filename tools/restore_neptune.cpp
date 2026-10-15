// Standalone tool to restore Neptune controller to default (lizard mode).
// Use it if WinDeck exits abnormally and the controller stays without its
// default mouse/keyboard emulation. Built by CMake as target `restore_neptune`.
#include <cstdio>
#include <cstdint>
#include <hidapi.h>

int main() {
    printf("Restoring Neptune controller to default state...\n");

    if (hid_init() != 0) {
        printf("ERROR: Failed to init hidapi\n");
        return 1;
    }

    // Find vendor-specific interface
    hid_device* dev = nullptr;
    struct hid_device_info* devs = hid_enumerate(0x28DE, 0x1205);
    for (auto* cur = devs; cur; cur = cur->next) {
        if (cur->usage_page == 0xFFFF && cur->usage == 0x0001) {
            dev = hid_open_path(cur->path);
            printf("Opened: %s\n", cur->path);
            break;
        }
    }
    hid_free_enumeration(devs);

    if (!dev) {
        printf("ERROR: Neptune controller not found\n");
        hid_exit();
        return 1;
    }

    // ID_SET_DEFAULT_DIGITAL_MAPPINGS (0x85)
    uint8_t cmd1[65] = {};
    cmd1[0] = 0x00;
    cmd1[1] = 0x85;
    hid_send_feature_report(dev, cmd1, sizeof(cmd1));
    printf("Sent: SET_DEFAULT_DIGITAL_MAPPINGS\n");

    // ID_LOAD_DEFAULT_SETTINGS (0x8E)
    uint8_t cmd2[65] = {};
    cmd2[0] = 0x00;
    cmd2[1] = 0x8E;
    hid_send_feature_report(dev, cmd2, sizeof(cmd2));
    printf("Sent: LOAD_DEFAULT_SETTINGS\n");

    // Read ack
    uint8_t buf[65] = {};
    hid_get_feature_report(dev, buf, sizeof(buf));

    hid_close(dev);
    hid_exit();

    printf("Done! Neptune should be back to normal.\n");
    return 0;
}
