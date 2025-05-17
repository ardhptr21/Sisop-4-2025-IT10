FROM ubuntu:latest

FROM ubuntu:20.04

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
    fuse gcc make && \
    mkdir -p /app

COPY antink.c /app/
WORKDIR /app

RUN gcc -Wall antink.c `pkg-config fuse --cflags --libs` -o antink

CMD ["./antink", "/antink_mount"]