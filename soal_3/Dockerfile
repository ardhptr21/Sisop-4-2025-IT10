FROM ubuntu:22.04

RUN apt update && \
    apt install -y libfuse* pkg-config gcc

COPY ./antink.c /antink.c

RUN gcc -Wall /antink.c $(pkg-config fuse --cflags --libs) -o /antink && \
    chmod +x /antink

RUN mkdir -p /antink_mount

CMD /antink -f /antink_mount