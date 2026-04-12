# Mail Service – Fall Detection Email Alerts

Node.js microservice that sends email notifications when a fall is detected and not cancelled. Uses nodemailer with Google SMTP.

## Features

- 📧 **Email Alerts**: Sends formatted HTML emails on fall detection
- 🔐 **Secure SMTP**: Google SMTP with app-specific passwords
- ⏱️ **Rate Limiting**: Per-device cooldown (60s) to prevent email spam
- 🏥 **Professional Templates**: Formatted HTML email with device ID and timestamp
- 🐳 **Docker Ready**: Fully containerized with health checks

## Setup

### 1. Google SMTP Configuration

This service requires a Gmail account and an **app-specific password** (not your regular Gmail password).

#### Generate App Password:

1. Go to [Google Account Security](https://myaccount.google.com/security)
2. Enable **2-Step Verification** (if not already enabled)
3. Navigate to **App passwords** (visible only after 2FA is enabled)
4. Select **Mail** and **Windows Computer** (or your device)
5. Google will generate a 16-character password
6. Copy this password to `.env` as `SMTP_PASS`

> ⚠️ **Important**: Use the app-specific password, NOT your regular Gmail password.

### 2. Environment Configuration

Create a `.env` file in the `mail-service/` directory:

```bash
cp .env.example .env
```

Edit `.env` and fill in:

```env
MAIL_SERVICE_PORT=3001
SMTP_HOST=smtp.gmail.com
SMTP_PORT=587
SMTP_USER=your-email@gmail.com           # Your Gmail address
SMTP_PASS=xxxx xxxx xxxx xxxx            # 16-char app password (no spaces)
ALERT_EMAIL=shreeyash.santosh2023@vitstudent.ac.in
```

### 3. Install Dependencies

```bash
cd mail-service
npm install
```

### 4. Run Locally

```bash
npm start
```

You should see:

```
╔═══════════════════════════════════════════════════════════╗
║   Fall Detection – Mail Service                           ║
╚═══════════════════════════════════════════════════════════╝

[SERVER] 🚀 Mail service running on port 3001
[CONFIG] SMTP Host: smtp.gmail.com
[CONFIG] Alert recipient: shreeyash.santosh2023@vitstudent.ac.in
[CONFIG] Cooldown: 60s per device

[API] POST http://localhost:3001/api/send-alert
[API] GET  http://localhost:3001/health
```

## API Endpoints

### Health Check

```bash
GET /health
```

Response:

```json
{
  "status": "ok",
  "service": "mail-service"
}
```

### Send Alert Email

```bash
POST /api/send-alert
```

Request body:

```json
{
  "device_id": "ESP32-NODE-1",
  "timestamp": 1711627548000
}
```

Response (success):

```json
{
  "status": "sent",
  "message": "Fall alert email sent successfully",
  "messageId": "<message-id@gmail.com>"
}
```

Response (cooldown):

```json
{
  "status": "cooldown",
  "message": "Alert already sent recently for this device"
}
```

Response (error):

```json
{
  "status": "error",
  "message": "Error details..."
}
```

## Testing

### Test endpoint with curl

```bash
curl -X POST http://localhost:3001/api/send-alert \
  -H "Content-Type: application/json" \
  -d '{"device_id":"ESP32-NODE-1","timestamp":'$(date +%s000)'}'
```

### Docker

Build and run:

```bash
docker build -t fall-detection-mail-service .
docker run -p 3001:3001 --env-file .env fall-detection-mail-service
```

## Docker Compose

The mail-service is automatically included in `docker-compose.yml`:

```bash
docker-compose up -d mail-service
```

## Integration with ESP32

The ESP32 firmware calls this endpoint when the alarm LED activates (after cancel window expires):

```cpp
// In blinkLED() function
POST http://10.248.21.212:3001/api/send-alert
{
  "device_id": "ESP32-NODE-1",
  "timestamp": 1711627548000
}
```

## Rate Limiting

- **Cooldown per device**: 60 seconds
- **Behavior**: If multiple falls detected within 60s, only the first email is sent
- **Purpose**: Prevents email spam if sensor flickers or device restarts

## Email Template

Static HTML template located at `templates/fallAlert.html`:

- Device ID and timestamp displayed
- Professional red alert styling
- Mobile-responsive design
- No external dependencies (all CSS inline)

## Troubleshooting

### SMTP Connection Failed

```
SMTP error: Invalid login (Incorrect username or password)
```

**Solution**: Check that:
1. You're using an **app-specific password**, NOT your Gmail password
2. 2FA is enabled on the Gmail account
3. `SMTP_USER` and `SMTP_PASS` are correct in `.env`

### Email Not Received

1. Check the service logs for errors
2. Verify `ALERT_EMAIL` is correct
3. Check Gmail spam folder
4. Test with your own email first: `ALERT_EMAIL=your-email@gmail.com`

### Rate Limit / Cooldown

If you're testing and emails aren't being sent:

```
[MAIL] ⏳ Device ESP32-NODE-1 on cooldown – skipping email
```

**Solution**: Wait 60 seconds or restart the service.

## Security Notes

- ✅ Use app-specific passwords (more secure than plain passwords)
- ✅ Do NOT commit `.env` file with secrets
- ✅ `.env` is already in `.gitignore`
- ✅ Use environment variables in production (not `.env` file)
- ✅ Email content is HTML-safe; no injection risks

## License

MIT
