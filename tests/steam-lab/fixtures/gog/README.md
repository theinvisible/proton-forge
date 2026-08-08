# GOG fixtures

Captured shapes of GOG's content-system and account responses, hand-trimmed to
the fields ProtonForge actually reads. The app's real parsers do the real work —
there are no mocks here, in keeping with the rest of the suite.

**Nothing here is real account data.** Specifically scrubbed:

- The signed CDN tokens in `secure-link.json` are fabricated. A captured one is a
  live credential: it grants anonymous read access to that product's chunks until
  it expires. The *structure* is kept — two vendors' token shapes plus a
  `fallback_only` endpoint — because `secureLinkExpiry` and the endpoint
  ordering are tested against it.
- No user ids, persona names or refresh tokens appear in any of these files.
- The paths in `gog-installs.json` are under a fabricated `/home/user`, so a test
  that ever acted on one would fail loudly rather than touch a real install.

## The `.zlib` files

Every content-system v2 body is zlib-encoded on the wire, with no
`Content-Encoding` header to say so. The `.json` files are the inflated form the
parsers take; the `.json.zlib` files are what the network hands over, and they
are what `inflateData` is tested against.

Regenerate after editing the JSON:

```bash
python3 - <<'PY'
import zlib, pathlib
for name in ("build-meta", "depot-manifest"):
    src = pathlib.Path(f"tests/steam-lab/fixtures/gog/{name}.json")
    pathlib.Path(f"tests/steam-lab/fixtures/gog/{name}.json.zlib").write_bytes(
        zlib.compress(src.read_bytes(), 9))
PY
```

## What each file is for

| File | Exercises |
|---|---|
| `builds-generation2.json` | the generation filter (it contains a gen-1 item), branch and public filtering, newest-wins |
| `build-meta.json` | depot selection: a shared depot, two language depots, a DLC depot, a 32-bit depot |
| `depot-manifest.json` | a chunked file, an unchunked one, a directory, a symlink, `executable` and `support` flags, backslash paths, and a `../` path that must be refused |
| `secure-link.json` | URL templating, expiry extraction, endpoint ordering |
| `goggame-windows.info` | play-task selection: a hidden config tool first, a non-primary game task *before* the primary one (so "first game task" and "the primary one" are different answers), a URLTask, and `arguments` as a string |
| `goggame-linux.info` | the native shape: `start.sh` and `arguments` as an array |
| `gog-installs.json` | the registry format: a complete Windows install, a complete Linux one with an update waiting, and an incomplete download that must not be offered as a game |
