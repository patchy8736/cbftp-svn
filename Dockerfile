FROM alpine
RUN apk add g++ make openssl-dev openssl-libs-static ncurses-dev ncurses-static musl-dev zlib-static
COPY . /app
WORKDIR /app
CMD ["sh", "-c", "make -j$(nproc) LINKFLAGS='-static -lssl -lcrypto -lpthread -lncurses'"]
