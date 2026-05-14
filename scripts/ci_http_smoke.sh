#!/usr/bin/env bash
set -euo pipefail
# Optional staging check: set repo variable NVR_CI_HTTP_SMOKE_URL (e.g. https://nvr-staging.example)
BASE="${NVR_CI_HTTP_SMOKE_URL:-}"
if [[ -z "${BASE}" ]]; then
  echo "ci_http_smoke: NVR_CI_HTTP_SMOKE_URL not set, skipping."
  exit 0
fi
BASE="${BASE%/}"
curl -fsS "${BASE}/api/v1/health" >/dev/null
echo "ci_http_smoke: OK ${BASE}/api/v1/health"
