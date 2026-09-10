#!/usr/bin/env bash
set -euo pipefail

required_names=(
    DASHSCOPE_API_KEY
    DASHSCOPE_WORKSPACE_ID
    EDGI_DEVICE_ID
    EDGI_DEVICE_TOKEN
    EDGI_LISTEN_HOST
    EDGI_LISTEN_PORT
)

check_env_file() {
    local env_file="$1"
    local line key value
    declare -A values=()

    if [[ ! -r "$env_file" ]]; then
        echo "environment file is not readable" >&2
        return 1
    fi
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%$'\r'}"
        [[ -z "$line" || "$line" == \#* || "$line" != *=* ]] && continue
        key="${line%%=*}"
        value="${line#*=}"
        values["$key"]="$value"
    done < "$env_file"

    for key in "${required_names[@]}"; do
        value="${values[$key]:-}"
        if [[ -z "$value" || "$value" == "UNSET" ]]; then
            echo "$key is not configured" >&2
            return 1
        fi
    done
    if (( ${#values[EDGI_DEVICE_TOKEN]} < 32 )); then
        echo "EDGI_DEVICE_TOKEN must contain at least 32 characters" >&2
        return 1
    fi
    if [[ ! "${values[EDGI_LISTEN_PORT]}" =~ ^[0-9]+$ ]] ||
       (( values[EDGI_LISTEN_PORT] < 1 || values[EDGI_LISTEN_PORT] > 65535 )); then
        echo "EDGI_LISTEN_PORT is invalid" >&2
        return 1
    fi
    echo "environment OK"
}

if [[ "${1:-}" == "--check-env" ]]; then
    [[ $# -eq 2 ]] || { echo "usage: $0 --check-env FILE" >&2; exit 2; }
    check_env_file "$2"
    exit $?
fi

if [[ $EUID -ne 0 ]]; then
    echo "run this installer as root" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/.." && pwd)"
app_dir="/opt/edgi-voice"
config_dir="/etc/edgi-voice"
env_file="$config_dir/edgi-voice.env"

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y python3-venv nginx

if ! id -u edgi-voice >/dev/null 2>&1; then
    useradd --system --home-dir "$app_dir" --shell /usr/sbin/nologin edgi-voice
fi

install -d -o root -g edgi-voice -m 0750 "$app_dir" "$app_dir/ecs" "$config_dir"
cp -a "$project_dir/ecs/." "$app_dir/ecs/"

if [[ ! -f "$env_file" ]]; then
    install -o root -g edgi-voice -m 0600 "$script_dir/edgi-voice.env.example" "$env_file"
    echo "created $env_file; fill it and run the installer again" >&2
    exit 2
fi
chown root:edgi-voice "$env_file"
chmod 0600 "$env_file"
check_env_file "$env_file" >/dev/null

python3 -m venv "$app_dir/venv"
"$app_dir/venv/bin/python" -m pip install --upgrade pip
"$app_dir/venv/bin/python" -m pip install -r "$app_dir/ecs/requirements.txt"

install -o root -g root -m 0644 "$script_dir/edgi-voice.service" /etc/systemd/system/edgi-voice.service
install -o root -g root -m 0644 "$script_dir/nginx-edgi-voice.conf" /etc/nginx/sites-available/edgi-voice
if [[ -e /etc/nginx/sites-enabled/default || -L /etc/nginx/sites-enabled/default ]]; then
    mv /etc/nginx/sites-enabled/default /etc/nginx/sites-available/default.disabled
fi
ln -sfn /etc/nginx/sites-available/edgi-voice /etc/nginx/sites-enabled/edgi-voice

nginx -t
systemctl daemon-reload
systemctl enable --now edgi-voice
systemctl enable --now nginx
systemctl restart nginx

curl --fail --silent http://127.0.0.1:8000/healthz
echo
echo "Edgi-Talk voice gateway installed"
