# Lab 2: Configure Your Own ESP-Claw

In this lab, you will independently configure an ESP-Claw edge AI agent. You will select compatible hardware, flash the ESP-Claw firmware, connect the device to Wi-Fi, configure an LLM provider, personalize the agent, and demonstrate a successful conversation.

ESP-Claw is Espressif's chat-coding AI agent framework for IoT devices. It combines an LLM with local capabilities, memory, event routing, and Lua automation so that an ESP device can sense, reason, and act as an edge agent.

This is an open-ended setup lab. You are responsible for reading the official documentation, making appropriate configuration choices, and documenting your result.

## Goals

By the end of this lab, you will be able to:

1. Identify hardware that satisfies the ESP-Claw requirements.
2. Flash firmware using the ESP-Claw web flasher.
3. Configure Wi-Fi and an LLM provider without exposing credentials.
4. Access and inspect the ESP-Claw Web Console.
5. Personalize and test your own edge AI agent.
6. Diagnose common connection and configuration problems.

## What you need

- An [ESP-Claw-supported development board](https://esp-claw.com/en/tutorial/supported-list/)
- At least **8 MB Flash and 8 MB PSRAM** on the selected board
- USB data cable
- Computer with Google Chrome or Microsoft Edge
- 2.4 GHz Wi-Fi for most supported boards
- An API key for a supported LLM provider
- Optional peripherals such as a display, LED, sensor, camera, microphone, speaker, or servo

The official web flasher supports selected board configurations. If your board is not listed, it may require a local source build and board adaptation, which is outside the basic requirements of this lab.

## Before you begin

Flashing ESP-Claw replaces the program currently stored on the board. Do not use a board whose existing firmware or data you need to preserve.

You will handle passwords, API keys, and possibly bot tokens during this lab. Follow these rules:

- Never write credentials in this repository.
- Never include credentials in screenshots or screen recordings.
- Do not share exported ESP-Claw configuration files or NVS dumps.
- Treat API usage as billable unless your provider explicitly states otherwise.
- Use a limited-purpose API key and revoke it after the course if it is no longer needed.
- Keep the ESP-Claw Web Console on a trusted local network; do not expose it to the public Internet.

## 1. Choose your hardware

Open the official [supported chips and boards page](https://esp-claw.com/en/tutorial/supported-list/) and identify the exact board you will use.

Record the following information in your lab notes:

| Item | Your configuration |
| --- | --- |
| Board manufacturer | |
| Board model | |
| ESP chip | |
| Flash capacity | |
| PSRAM capacity | |
| Built-in peripherals | |
| External peripherals, if any | |

Confirm that your board has at least 8 MB Flash and 8 MB PSRAM. A chip family name alone is not enough because the same chip may be sold with several memory configurations.

If you are assembling a breadboard version, follow the official [materials](https://esp-claw.com/en/tutorial/bom/) and [assembly](https://esp-claw.com/en/tutorial/assemble/) guides. Disconnect power before changing any wiring and verify all power, ground, and signal connections before reconnecting the board.

## 2. Plan your ESP-Claw

Before flashing, decide what kind of agent you want to create. Write a short plan that answers these questions:

1. What is your agent's name and purpose?
2. How will you interact with it: Web Chat, Telegram, WeChat, QQ, Feishu, or another supported channel?
3. Which built-in or external hardware will it use?
4. What is one simple task it should complete by the end of this lab?
5. How will you know that the task succeeded?

Keep the first task small and testable. Examples include answering a question in Web Chat, remembering a non-sensitive preference, describing its available capabilities, or controlling a supported LED.

## 3. Flash ESP-Claw

1. Open the official [ESP-Claw web flasher](https://esp-claw.com/en/flash/) in Chrome or Edge.
2. Connect the board to your computer with a USB data cable.
3. Click **Connect** and select the serial device that belongs to your board.
4. Select the application, chip, manufacturer, and exact board model requested by the flasher.
5. Verify the detected Flash and PSRAM configuration before continuing.
6. Leave **Erase entire flash before flashing** disabled unless you need a clean installation or are troubleshooting an old configuration.
7. Start the flash process and keep the board connected until it completes.
8. Reconnect the serial device if the browser asks after the board resets.

Do not guess the board definition. Firmware built for a different board may use the wrong pins or peripherals.

## 4. Connect ESP-Claw to Wi-Fi

After flashing, the web flasher should prompt you for a Wi-Fi SSID and password.

1. Enter the credentials for a trusted network.
2. Connect the board to Wi-Fi.
3. Wait for the flasher to report that the device is online.
4. Record the device IP address, but do not record the Wi-Fi password.

Most supported boards currently require 2.4 GHz Wi-Fi. If the course network uses enterprise authentication or prevents local devices from communicating, use a permitted 2.4 GHz personal hotspot or another network approved by your instructor.

If browser-based provisioning does not continue, connect your computer to the board's SoftAP. Its default name is similar to:

```text
esp-claw-XXXXXX
```

Then open the configuration page and enter the Wi-Fi settings there.

## 5. Open the Web Console

Connect your computer to the same local network as ESP-Claw and open:

```text
http://esp-claw.local/
```

If the `.local` address does not resolve, use the board's IP address:

```text
http://<device-ip>/
```

In the Web Console, confirm that the System Status page shows:

- The expected board is online.
- Wi-Fi is connected.
- The device has an IP address.
- The system is not continuously restarting.

The Web Console uses HTTP on the local network and may display sensitive configuration information. Only open it on a trusted network, and do not publish or port-forward it.

## 6. Configure the LLM

Open the LLM settings in the Web Console.

1. Select a supported provider or an API-compatible custom provider.
2. Enter your API key.
3. Enter a model name that is available to your account.
4. If using a custom provider, enter its base URL in the format required by the official documentation.
5. Save the settings and restart ESP-Claw when prompted.

ESP-Claw supports OpenAI-compatible and Anthropic-compatible APIs. Available model names and charges depend on the provider and your account. Begin with short tests and monitor your API usage.

Do not show the API key in your submission. A screenshot should show only that the provider and model are configured; crop or cover every secret field.

## 7. Personalize your agent

Give the device a simple identity appropriate for your project. In the relevant Web Console memory or identity settings, define:

- Agent name
- Purpose or role
- Preferred response style
- Relevant hardware context

Do not store personal, confidential, or regulated information on the device. For this lab, use fictional or non-sensitive preferences only.

Example identity:

```text
Name: Orbit
Role: AIoT lab assistant
Style: concise and technical
Hardware: ESP32 board with its built-in status LED
```

## 8. Complete the interaction tests

Use Web Chat or a configured messaging channel to complete all three tests.

### Test A: Basic conversation

Send:

```text
Hello. What capabilities do you currently have?
```

Confirm that ESP-Claw returns a relevant response without an API or timeout error.

### Test B: Personalization

Ask the agent to report its name and purpose. Confirm that the response matches the identity you configured.

### Test C: Your own task

Run the small task you planned in Step 2. Record:

- Your prompt
- The expected behavior
- The observed behavior
- Whether the test passed
- One improvement you would make

If the first attempt fails, revise your prompt or configuration and try again. Document at least one iteration rather than hiding unsuccessful attempts.

## Optional extension: Add an interaction channel

Web Chat is sufficient for the required lab. For an extension, configure one supported messaging platform and send a message to ESP-Claw from that platform.

Depending on the platform, you may need a bot token, app ID, app secret, permissions, or QR-code authorization. Save and restart the device after changing IM settings. Never include the token or secret in your submission.

## Expected result

You have completed Lab 2 when all of the following are true:

- Your board satisfies the ESP-Claw memory requirements.
- ESP-Claw firmware boots successfully.
- The device connects to Wi-Fi and appears online in the Web Console.
- An LLM provider and model are configured.
- ESP-Claw responds successfully through Web Chat or another supported channel.
- The agent reports its configured identity.
- You complete and document one self-designed task.

## Troubleshooting

| Symptom | Likely cause | What to try |
| --- | --- | --- |
| The browser cannot connect to the board | Unsupported browser, charge-only cable, or serial port already in use | Use Chrome or Edge, use a USB data cable, and close Arduino Serial Monitor and other serial tools. |
| No compatible firmware appears | Unsupported board or insufficient Flash/PSRAM | Verify the exact board model and memory configuration against the supported-board list. |
| Flashing cannot start | Board is not in download mode | Follow the board-specific boot/download procedure, reconnect it, and select the port again. |
| Wi-Fi does not connect | Incorrect credentials, 5 GHz-only network, weak signal, or incompatible network authentication | Re-enter the case-sensitive credentials and try an approved 2.4 GHz network closer to the device. |
| `esp-claw.local` does not open | mDNS is unavailable on the network | Use the IP address shown by the flasher, serial console, or router instead. |
| ESP-Claw does not answer | Invalid API key, unavailable model, network restriction, or provider error | Check the LLM provider, model name, API balance, and serial or web-console logs. |
| The agent gives irrelevant answers | Weak model, long conversation history, or unclear prompt | Start a new session, choose a more capable model, and give a smaller, explicit task. |
| Messaging app receives no reply | Incorrect bot credentials or permissions | Test Web Chat first, then verify the messaging platform settings and inspect logs. |
| The board repeatedly restarts | Invalid configuration, incompatible firmware, power issue, or peripheral wiring problem | Disconnect optional peripherals, inspect serial logs, and reflash the correct board firmware if necessary. |

## Submission checklist

Submit one short report containing:

1. The completed hardware table from Step 1.
2. Your ESP-Claw plan from Step 2.
3. A photo of your physical setup.
4. A screenshot showing successful firmware flashing.
5. A screenshot of System Status showing that the device is online.
6. A screenshot of one successful conversation.
7. The prompt, expected result, observed result, and improvement from your self-designed task.
8. A short reflection: How is an edge agent different from the fixed Arduino programs used in earlier labs?

Before submission, inspect every image and document. Remove all Wi-Fi passwords, API keys, bot tokens, app secrets, QR codes, and other credentials.

## References

- [ESP-Claw website](https://esp-claw.com/en/)
- [ESP-Claw documentation](https://esp-claw.com/en/tutorial/)
- [Supported chips and development boards](https://esp-claw.com/en/tutorial/supported-list/)
- [Web flashing guide](https://esp-claw.com/en/tutorial/get-started/)
- [ESP-Claw web flasher](https://esp-claw.com/en/flash/)
- [Web Console guide](https://esp-claw.com/en/tutorial/web-config/)
- [First interactions](https://esp-claw.com/en/tutorial/first-interactions/)
- [ESP-Claw FAQ](https://esp-claw.com/en/tutorial/faq/)
- [ESP-Claw source code](https://github.com/espressif/esp-claw)
