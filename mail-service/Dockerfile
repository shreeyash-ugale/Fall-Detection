# Use official Node.js runtime as base image
FROM node:18-alpine

# Install tzdata for timezone support
RUN apk add --no-cache tzdata

# Set timezone to IST (Asia/Kolkata)
ENV TZ=Asia/Kolkata

# Set working directory
WORKDIR /app

# Copy package files
COPY package*.json ./

# Install dependencies
RUN npm install --production

# Copy application files
COPY . .

# Expose port
EXPOSE 3001

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
  CMD node -e "require('http').get('http://localhost:3001/health', (r) => {if (r.statusCode !== 200) throw new Error(r.statusCode)})"

# Start server
CMD ["npm", "start"]
