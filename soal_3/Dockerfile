FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    pkg-config \
    libfuse-dev \
    fuse

WORKDIR /app
COPY antink.c .

RUN gcc -Wall `pkg-config fuse --cflags` antink.c -o antink `pkg-config fuse --libs`

RUN mkdir -p /app/antink_mount && chmod 777 /app/antink_mount

ENTRYPOINT ["./antink"]
CMD ["/app/antink_mount", "-f"]