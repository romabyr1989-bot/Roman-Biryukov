# Parkinome Core Architecture

## Active Runtime Modules

- `server.c`: HTTP server, routing, RBAC checks, static UI delivery.
- `auth.c` / `auth.h`: token-to-role mapping (`doctor`, `researcher`, `admin`).
- `predictor.c` / `predictor.h`: JSON parsing (`cJSON`), single and batch prediction handlers.
- `model.c` / `predict.h`: risk model and public prediction API.
- `json_io.c` / `json_io.h`: file read utility (used for `index.html`).
- `index.html`: built-in UI served from `/`.

## Legacy Modules

Moved to `core/legacy/` to separate non-runtime or historical code paths:

- `features.c`
- `validate.c`
- `storage.c` / `storage.h`
- `storage_sqlite.c` / `storage_sqlite.h`
- `logger.c` / `logger.h`
- `model_weights.h`

These files are preserved for reference and possible future reintegration, but are not part of the current HTTP prediction path.

## Request Flow

1. Client sends HTTP request to `server.c`.
2. `Authorization` header is parsed and role is resolved in `auth.c`.
3. Route-level RBAC is enforced in `server.c`.
4. `predictor.c` parses request JSON and calls `parkinome_predict(...)`.
5. `model.c` computes risk metrics and response values.
6. `server.c` returns JSON response.
