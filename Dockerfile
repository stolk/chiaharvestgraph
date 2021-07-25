FROM alpine/make
RUN apk add build-base

COPY . .
RUN make
ENTRYPOINT ./chiaharvestgraph /.chia/mainnet/log
