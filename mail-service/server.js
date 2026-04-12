// ============================================================
//  mail-service/server.js – Fall Detection Email Alert Service
// ============================================================

require('dotenv').config();
const express = require('express');
const nodemailer = require('nodemailer');
const cors = require('cors');
const fs = require('fs');
const path = require('path');

const app = express();
const PORT = process.env.MAIL_SERVICE_PORT || 3001;

// Middleware
app.use(cors());
app.use(express.json());

// ============================================================
//  SMTP Configuration
// ============================================================

const transporter = nodemailer.createTransport({
  host: process.env.SMTP_HOST || 'smtp.gmail.com',
  port: parseInt(process.env.SMTP_PORT || '587'),
  secure: false, // true for 465, false for other ports
  auth: {
    user: process.env.SMTP_USER,
    pass: process.env.SMTP_PASS,
  },
});

// Test SMTP connection on startup
transporter.verify((error, success) => {
  if (error) {
    console.error('[SMTP] ❌ Connection verification failed:');
    console.error(error);
  } else {
    console.log('[SMTP] ✅ Server is ready to take our messages');
  }
});

// ============================================================
//  Email Template Loader
// ============================================================

function loadEmailTemplate(deviceId, timestamp, istFormattedString) {
  const templatePath = path.join(__dirname, 'templates', 'fallAlert.html');
  let html = fs.readFileSync(templatePath, 'utf8');

  // Replace placeholders
  const date = new Date(timestamp);
  
  const dateShort = new Intl.DateTimeFormat('en-GB', {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    timeZone: 'Asia/Kolkata'
  }).format(date);
  
  const timeShort = new Intl.DateTimeFormat('en-GB', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
    timeZone: 'Asia/Kolkata'
  }).format(date);

  html = html.replace('{{DEVICE_ID}}', deviceId);
  html = html.replace('{{TIMESTAMP}}', istFormattedString);
  html = html.replace('{{DATE_SHORT}}', dateShort);
  html = html.replace('{{TIME_SHORT}}', timeShort);

  return html;
}

// ============================================================
//  Rate Limiting: Per-device cooldown to prevent spam
// ============================================================

const lastAlertTime = {}; // {device_id: timestamp}
const COOLDOWN_MS = 5 * 1000; // 5 seconds

function isOnCooldown(deviceId) {
  const lastTime = lastAlertTime[deviceId];
  if (!lastTime) return false;

  const timeSinceLastAlert = Date.now() - lastTime;
  return timeSinceLastAlert < COOLDOWN_MS;
}

function updateCooldown(deviceId) {
  lastAlertTime[deviceId] = Date.now();
}

// ============================================================
//  API Endpoints
// ============================================================

/**
 * Get current time in IST from internet
 */
async function getCurrentISTTime() {
  try {
    const response = await fetch('http://worldtimeapi.org/api/timezone/Asia/Kolkata');
    const data = await response.json();
    return new Date(data.datetime);
  } catch (error) {
    console.log('[TIME] ⚠️  Failed to fetch internet time, using local time');
    return new Date();
  }
}

/**
 * Health check endpoint
 */
app.get('/health', (req, res) => {
  res.json({ status: 'ok', service: 'mail-service' });
});

/**
 * Send fall alert email
 * POST /api/send-alert
 * Body: { device_id: string, timestamp: number }
 *
 * Returns: { status: "sent" | "cooldown" | "error", message?: string }
 */
app.post('/api/send-alert', async (req, res) => {
  try {
    const { device_id, timestamp } = req.body;

    // Validate required fields
    if (!device_id || !timestamp) {
      return res.status(400).json({
        status: 'error',
        message: 'device_id and timestamp are required',
      });
    }

    // Format timestamp in IST
    const formattedDate = new Date(timestamp);
    
    // If timestamp is invalid or from 1970 (epoch), fetch current time from internet
    let dateToUse = formattedDate;
    if (isNaN(formattedDate.getTime()) || formattedDate.getFullYear() <= 1970) {
      console.log(`[MAIL] ⚠️  Invalid timestamp received, fetching current IST time from internet`);
      dateToUse = await getCurrentISTTime();
    }
    
    const options = {
      weekday: 'long',
      year: 'numeric',
      month: 'long',
      day: 'numeric',
      hour: 'numeric',
      minute: 'numeric',
      second: 'numeric',
      hour12: false,
      timeZone: 'Asia/Kolkata'
    };

    const formatter = new Intl.DateTimeFormat('en-GB', options);
    const istFormattedString = formatter.format(dateToUse);

    // Check cooldown
    if (isOnCooldown(device_id)) {
      console.log(`[MAIL] ⏳ Device ${device_id} on cooldown – skipping email`);
      return res.status(429).json({
        status: 'cooldown',
        message: 'Alert already sent recently for this device',
      });
    }

    // Update cooldown
    updateCooldown(device_id);

    // Load and render email template
    const htmlContent = loadEmailTemplate(device_id, dateToUse.getTime(), istFormattedString);

    // Prepare email options
    const mailOptions = {
      from: `Fall Alert System <${process.env.SMTP_USER}>`,
      to: process.env.ALERT_EMAIL,
      subject: `Fall Detected – Device: ${device_id}`,
      html: htmlContent,
    };

    // Send email
    console.log(`[MAIL] Sending alert email for device ${device_id}...`);
    const info = await transporter.sendMail(mailOptions);

    console.log(`[MAIL] Email sent successfully – Response: ${info.response}`);
    console.log(`[MAIL] Message ID: ${info.messageId}`);

    res.status(200).json({
      status: 'sent',
      message: 'Fall alert email sent successfully',
      messageId: info.messageId,
    });
  } catch (error) {
    console.error(`[MAIL] ❌ Error sending email:`, error);
    res.status(500).json({
      status: 'error',
      message: error.message,
    });
  }
});

// ============================================================
//  Server Startup
// ============================================================

app.listen(PORT, () => {
  console.log('\n');
  console.log('╔═══════════════════════════════════════════════════════════╗');
  console.log('║   Fall Detection – Mail Service                           ║');
  console.log('╚═══════════════════════════════════════════════════════════╝');
  console.log(`\n[SERVER] 🚀 Mail service running on port ${PORT}`);
  console.log(`[CONFIG] SMTP Host: ${process.env.SMTP_HOST}`);
  console.log(`[CONFIG] Alert recipient: ${process.env.ALERT_EMAIL}`);
  console.log(`[CONFIG] Cooldown: ${COOLDOWN_MS / 1000}s per device`);
  console.log(`\n[API] POST http://localhost:${PORT}/api/send-alert`);
  console.log(`[API] GET  http://localhost:${PORT}/health\n`);
});
