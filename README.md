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

1. Install the [pebble-tool](https://pypi.org/project/pebble-tool/) (Python 3.10+): `pip install pebble-tool` (or `uv tool install pebble-tool`)
2. Install the SDK: `pebble sdk install latest`
3. Clone this repository
4. Build: `pebble build`
5. Install: `pebble install`

### 2. Configuration

1. Open the application settings on your phone (or run `./open-clay-config.py` in the emulator)
2. Enter your Hermes or OpenClaw bot username (e.g., `@MySquireBot`) and press Save
3. Sign in to Telegram, either from the watch or from the settings page:
   - **From the watch**: launch Squire and follow the prompts — enter your
     phone number in international format (e.g., `+1234567890`), then the
     verification code Telegram sends you.
   - **From the settings page**: enter your phone number in the *Telegram
     Sign-In* section and press Save. Telegram sends you a login code; reopen
     the settings page, enter the code, and press Save again.
   - **Two-step verification (2FA)**: the cloud password can't be entered on
     the watch — enter it in the *Telegram Sign-In* section of the settings
     page along with the login code. Sign-in fields are cleared after each
     attempt and never stored.
4. Use the **Disconnect** option in the watch app's More menu to sign out of
   Telegram (this also revokes the session server-side)

### Agent integration notes

- Squire prefixes each prompt with a `<system>...</system>` block containing
  device context, and continues conversations with a `[thread:<id>]` prefix.
- Squire normally decides a reply is finished after ~2 seconds without a new
  message. If your agent sends multi-part replies with longer pauses, have it
  end its final message with `[done]` — Squire strips the marker and closes
  the conversation immediately.

## Development

### Project Structure

```
src/
├── c/                 # Watch app C code
├── pkjs/              # Phone app JavaScript
│   ├── telegram/      # Telegram MTProto client
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