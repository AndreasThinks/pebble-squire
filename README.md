# Squire (formerly Bobby Assistant)

Squire is an AI assistant that runs on your Pebble smartwatch, connecting to a [Hermes](https://github.com/nousresearch/hermes-agent) or [OpenClaw](https://github.com/openclaw/openclaw) backend via Telegram.

## Prerequisites

Squire requires a **Telegram bot** connected to a running instance of **Hermes** or **OpenClaw**. Without one of these backends, Squire will not function.

### Setting up a Telegram Bot

1. Open a conversation with [@BotFather](https://t.me/BotFather) on Telegram
2. Send `/newbot` and follow the prompts to create your bot
3. Copy the bot API token — you'll need it for your backend configuration
4. Note your bot username (e.g., `@MySquireBot`)

### Setting up Hermes or OpenClaw

1. Install [Hermes](https://github.com/nousresearch/hermes-agent) or [OpenClaw](https://github.com/openclaw/openclaw) on a server or locally
2. Configure it with your Telegram bot token from BotFather
3. Make sure the backend is running and accessible

## Architecture

The phone app communicates directly with Telegram using MTProto, sending messages to your Hermes or OpenClaw bot instance. Since Squire just acts as a frontend for your agent, the potential features are limitless — anything you can configure your agent to do on your behalf works the same way on your watch.

**Flow:**
```
Watch App → Phone App (pkjs) → Telegram MTProto → Hermes/OpenClaw Bot
```

## Setup

### 1. Building the App

1. Install the [Pebble SDK](https://developer.rebble.io/developer.pebble.com/sdk/index.html)
2. Clone this repository
3. Build: `pebble build`
4. Install: `pebble install`

### 2. Configuration

1. Open the application settings on your phone (or run `./open-clay-config.py` in the emulator)
2. Enter your Hermes or OpenClaw bot username (e.g., `@MySquireBot`) and press Save
3. Launch Squire on the watch — it will prompt you to sign in to Telegram directly from the watch:
   - Enter your phone number in international format (e.g., `+1234567890`)
   - A verification code is sent to your Telegram app; enter it on the watch when prompted
   - If your account has 2FA enabled, you'll also be asked for your password
4. Use the **Disconnect** button in the phone settings to sign out of Telegram

## Development

### Project Structure

```
src/
├── c/                 # Watch app C code
├── pkjs/              # Phone app JavaScript
│   ├── telegram/      # Telegram MTProto client
│   ├── actions/       # Action handlers (alarms, reminders)
│   └── session.js     # Main session management
resources/             # Watch app resources (icons, images, etc.)
package.json           # Pebble app configuration
```

### Key Files

- `src/pkjs/telegram/` - GramJS-based Telegram client
- `src/pkjs/session.js` - Session management and backend communication
- `src/pkjs/config.json` - Settings UI configuration

## Security Considerations

- Telegram session is stored in localStorage (consider encryption for production)
- Phone numbers are used only during authentication, not stored

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for details.

## License

Apache 2.0; see [`LICENSE`](LICENSE) for details.

## Disclaimer

This project is not an official Google project. It is not supported by
Google and Google specifically disclaims all warranties as to its quality,
merchantability, or fitness for a particular purpose.