#include <filesystem>
#include <format>
#include <fstream>
#include <grp.h>
#include <map>
#include <mutex>
#include <print>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

import AcerHIDRGB;
import AcerHIDHardware;

namespace acerctrld {
    struct rgb {
        uint8_t r, g, b;
    };

	/* taken from acer predator sense */
    const std::map<uint8_t, rgb> usage_mode_colors = {
        { HW_USAGE_MODE_TURBO,        { 0xC7, 0x00, 0xFF } },
        { HW_USAGE_MODE_PERFORMANCE,  { 0x2E, 0x09, 0xC7 } },
        { HW_USAGE_MODE_NORMAL,       { 0x00, 0xAE, 0xC7 } },
        { HW_USAGE_MODE_QUIET,        { 0xFF, 0xFF, 0xFF } },
        { HW_USAGE_MODE_ECO,          { 0x10, 0xDC, 0x00 } }
    };

	bool hid_rgb_available = false;
	bool hid_hw_available = false;

    std::string socket_path = "/run/acerctrl.sock";

    std::mutex values_file_mutex;

	void saveValue(const std::string&);
    void loadLastValues();

	/* this is a stupid workaround for laptops like mine where the rgb and stuff doesnt turn off, so just do it outselves */
    void sleepEnter() {
        if (hid_rgb_available) {
            acerhidrgb::rgbSet("keyboard", "off", 0, 0, 0, 0, 0, 0, 0x0F);
            acerhidrgb::rgbSet("lid", "off", 100, 0, 0, 0, 0, 0, 0);
            acerhidrgb::rgbSet("profile_button", "static", 100, 0, 0, 0, 0, 0, 0);
        }

        if (hid_hw_available) {
            acerhidhw::setUsageMode(0x04);
        }
    }

    void sleepExit() {
        loadLastValues();
    }

    void usageModeRgbFlash(uint8_t mode) {
        if (usage_mode_colors.contains(mode)) {
            auto [r, g, b] = usage_mode_colors.at(mode);
            acerhidrgb::rgbSet("keyboard", "mode_change", 100, 0, 0, r, g, b, 0x0F);
        }
    }

	void setUsageModeAndRgb(uint8_t mode, bool save) {
		if (!acerhidhw::setUsageMode(mode)) {
			return;
		}

        usageModeRgbFlash(mode);

		if (save) {
		    saveValue(std::format("SET_USAGE_MODE {:d}", mode));
		}
	}

	void cycleUsageModeAndRgb() {
		uint8_t mode = acerhidhw::cycleUsageMode();

        usageModeRgbFlash(mode);

		saveValue(std::format("SET_USAGE_MODE {:d}", mode));
	}

	void handleRgb(const std::string& cmd, bool save) {
		std::string command_str, device, effect;
		int brightness, speed, direction, r, g, b, zone;

		std::stringstream ss(cmd);
		ss >> command_str >> device >> effect >> brightness >> speed >> direction >> r >> g >> b >> zone;

        if (ss.fail()) {
            std::println("[ERR] Malformed RGB command!");
            return;
        }

		if (acerhidrgb::rgbSet(device, effect, brightness, speed, direction, r, g, b, zone) && save) {
			saveValue(cmd);
		}
	}

	void handleTimeout(const std::string& cmd) {
		std::string command_str;
		int time;

		std::stringstream ss(cmd);
		ss >> command_str >> time;

        if (ss.fail()) {
            std::println("[ERR] Malformed timeout command!");
            return;
        }

		acerhidhw::keyboardTimeoutSet(time);
	}

	void handleBatteryLimits(const std::string& cmd) {
	    std::string command_str;
    	int status, lower, upper;

    	std::stringstream ss(cmd);
    	ss >> command_str >> status >> lower >> upper;

    	if (ss.fail()) {
    		std::println("[ERR] Malformed battery limit command!");
    		return;
    	}

    	acerhidhw::setBatteryLimits(status, lower, upper);
    }

	void handleUsageMode(const std::string& cmd, bool save) {
		std::string command_str;
		int mode;
		
		std::stringstream ss(cmd);
		ss >> command_str >> mode;

        if (ss.fail()) {
            std::println("[ERR] Malformed usage mode command!");
            return;
        }

		setUsageModeAndRgb(mode, save);
	}

	void handleCommand(const std::string& msg, bool save) {
		std::stringstream ss(msg);
		std::string command;
		ss >> command;

		if (command == "SET_RGB") {
			if (!hid_rgb_available) {
				std::println("[WARN] Got RGB command but RGB features are unavailable");
				return;
			} else {
				handleRgb(msg, save);
			}
		} else if (command == "SET_TIMEOUT") {
			if (!hid_hw_available) {
				std::println("[WARN] Got timeout command but hardware features are unavailable");
				return;
			} else {
				handleTimeout(msg);
			}
		} else if (command == "SET_USAGE_MODE") {
			if (!hid_hw_available) {
				std::println("[WARN] Got usage mode command but hardware features are unavailable");
				return;
			} else {
				handleUsageMode(msg, save);
			}
		} else if (command == "SET_BATTERY_LIMITS") {
			if (!hid_hw_available) {
				std::println("[WARN] Got battery limits command but hardware features are unavailable");
				return;
			} else {
				handleBatteryLimits(msg);
			}
        } else if (command == "SLEEP_ENTER") {
            sleepEnter();
        } else if (command == "SLEEP_EXIT") {
            sleepExit();
		} else {
			println("[ERR] Unknown command {}!", command);
			return;
		}
	}

	void loadLastValues() {
		if (!std::filesystem::exists("/var/lib/acerctrl/set_values")) {
			std::println("[ERR] Failed to load values from /var/lib/acerctrl/set_values!");
            return;
		}

		std::ifstream values_file("/var/lib/acerctrl/set_values");
		if (values_file.is_open()) {
			std::string line;
			while(std::getline(values_file, line)) {
				handleCommand(line, false);
			}
		} else {
			std::println("[ERR] Failed to open /var/lib/acerctrl/set_values!");
			return;
		}
	}

    /* TODO: this sucks tbh, can rewrite to make less awful */
	void saveValue(const std::string& setting) {
        /* in case turbo button is pressed when another command is being saved */
        std::lock_guard<std::mutex> lock(values_file_mutex);

		if (!std::filesystem::is_directory(std::filesystem::status("/var/lib/acerctrl"))) {
			std::println("[WARN] /var/lib/acerctrl/ does not exist, creating it...");

			if (!std::filesystem::create_directories("/var/lib/acerctrl")) {
				std::println("[ERR] Failed to create /var/lib/acerctrl/");
				return;
			}
		}

		if (!std::filesystem::exists("/var/lib/acerctrl/set_values")) {
			std::ofstream values_file("/var/lib/acerctrl/set_values");
			if (!values_file) {
				std::println("[ERR] Failed to create /var/lib/acerctrl/set_values!");
				return;
			}
			values_file.close();
		}

		std::ifstream values_file_in("/var/lib/acerctrl/set_values");
		if (values_file_in.is_open()) {
			std::stringstream ss(setting);
			std::string command;
			ss >> command;

			if (command != "SET_RGB") {
				if (command != "SET_USAGE_MODE") {
					return;
				}
			}

			std::vector<std::string> lines;
			std::string line;
			bool found = false;

            std::string cmd_dev_name;
            if (command == "SET_RGB") {
                ss >> cmd_dev_name;
            }

			while(std::getline(values_file_in, line)) {
                std::stringstream current_line_ss(line);
                std::string line_command;
                current_line_ss >> line_command;

				if (line_command == command) {
					if (line_command == "SET_RGB") {
						std::string line_dev_name;
                        current_line_ss >> line_dev_name;

						if (line_dev_name == cmd_dev_name) {
							lines.push_back(setting);
							found = true;
						} else {
							lines.push_back(line);
						}
					} else {
						lines.push_back(setting);
						found = true;
					}
				} else {
					lines.push_back(line);
				}
			}
			values_file_in.close();

			if (!found) {
				lines.push_back(setting);
			}

			std::ofstream values_file_out("/var/lib/acerctrl/set_values.tmp", std::ios::out | std::ios::trunc);
			for (const auto& line : lines) {
				values_file_out << line << "\n";
			}
			values_file_out.close();
		} else {
			std::println("[ERR] Failed to save values!");
		}

        std::filesystem::rename("/var/lib/acerctrl/set_values.tmp", "/var/lib/acerctrl/set_values");
	}

	void watchTurboButton() {
		for (;;) {
			acerhidhw::waitForTurboButtonEvent();
			cycleUsageModeAndRgb();
		}
	}

	void runSocket() {
		int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
		sockaddr_un addr {};
		addr.sun_family = AF_UNIX;
        if (socket_path.length() >= sizeof(addr.sun_path)) {
            std::println("[WARN] Socket path is too long, defaulting to /run/acerctrl.sock");
            socket_path = "/run/acerctrl.sock";
        }
		strncpy(addr.sun_path, socket_path.c_str(), socket_path.length());

		unlink(socket_path.c_str());
		if (bind(server_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
            std::println("[ERR] Failed to bind socket {}!", socket_path);
            return;
        }

        struct group *wheel_group = getgrnam("wheel");
        if (wheel_group) {
            if (chown(socket_path.c_str(), 0, wheel_group->gr_gid) < 0) {
                std::println("[WARN] Failed to change socket group to wheel");
            }
        } else {
            std::println("[WARN] Wheel group not found");
        }

        chmod(socket_path.c_str(), 0660);

		listen(server_fd, 5);

		for(;;) {
			int client_fd = accept(server_fd, nullptr, nullptr);

			char buf[256] = {0};
			ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                std::string command(buf);
                handleCommand(command, true);
            }

			close(client_fd);
		}
	}

} /* namespace acerctrld */

int main(int argc, char *argv[]) {
	if (geteuid() != 0) {
		std::println("[ERR] Must be run as root!");
		return -1;
	}

	/* it might(?) be possible for general hardware control to exist without RGB and vice versa, so we should account for that just in case */
	if (acerhidrgb::init()) {
		acerctrld::hid_rgb_available = true;
	} else {
		std::println("[WARN] RGB device not found, RGB features will be unavailable");
	}

	if (acerhidhw::init()) {
		acerctrld::hid_hw_available = true;
	} else {
		std::println("[WARN] Hardware device not found, hardware features will be unavailable");
	}

	if (!acerctrld::hid_rgb_available && !acerctrld::hid_hw_available) {
		std::println("[ERR] No features available!");
		return -1;
	}

	/* set rgb to white to stop rainbow puke if no config saved */
	if (acerctrld::hid_rgb_available) {
		acerhidrgb::rgbSet("keyboard", "static", 0x19, 0x00, 0x00, 255, 255, 255, 0x0F);
		acerhidrgb::rgbSet("lid", "static", 0x19, 0x00, 0x00, 255, 255, 255, 0x00);
	}

	acerctrld::loadLastValues();

	std::jthread turbo_button(acerctrld::watchTurboButton);

	acerctrld::runSocket();
}
