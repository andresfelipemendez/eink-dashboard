FROM alpine:3.20 AS builder

RUN apk add --no-cache gcc musl-dev libwebsockets-dev openssl-dev

WORKDIR /build
COPY src/ src/

# Production build - no hot-reload, no file watcher
RUN gcc -O2 -o loader src/loader_prod.c -ldl -lwebsockets && \
    gcc -O2 -DPRODUCTION -shared -fPIC -o app.so src/app.c

FROM alpine:3.20

RUN apk add --no-cache libwebsockets

WORKDIR /app
COPY --from=builder /build/loader /build/app.so ./

EXPOSE 8080

CMD ["./loader"]
