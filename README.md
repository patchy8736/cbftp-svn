# cbftp-svn Build Notes

Use Docker Compose to build binaries into `./bin`:

```bash
docker compose up --build
```

Useful variants:

```bash
# stop after builder exits
docker compose up --build --abort-on-container-exit

# one-shot builder container
docker compose run --build --rm builder

# force clean rebuild
docker compose build --no-cache
docker compose up --abort-on-container-exit
```
