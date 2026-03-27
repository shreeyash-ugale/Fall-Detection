FROM golang:1.25-alpine AS builder

WORKDIR /app

COPY go.mod go.sum ./
RUN go mod download

COPY . .

RUN go build -o app ./cmd/server

FROM alpine:latest

WORKDIR /root/

COPY --from=builder /app/app .

EXPOSE 8081

CMD ["./app"]