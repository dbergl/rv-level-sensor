#!/usr/bin/env bash
# Copy the Home Assistant package, templates, dashboard and image into an HA config directory,
# and enable packages in configuration.yaml if they aren't already.
#
#   tools/ha_install.sh /path/to/ha/config                 # HA config on this machine
#   tools/ha_install.sh pi@my-pi homeassistant/config            # over ssh
#
# Then restart Home Assistant.
set -euo pipefail

if [[ $# -eq 1 ]]; then
  host=""; dir=$1
elif [[ $# -eq 2 ]]; then
  host=$1; dir=$2
else
  echo "usage: $0 [user@host] <ha-config-dir>" >&2
  exit 2
fi

src="$(cd "$(dirname "$0")/.." && pwd)"
# source (relative to the repo) -> destination (relative to the HA config dir)
files=(homeassistant/packages/rv_level.yaml:packages/rv_level.yaml
       homeassistant/custom_templates/rv_level.jinja:custom_templates/rv_level.jinja
       homeassistant/dashboards/rv_level.yaml:dashboards/rv_level.yaml
       custom_components/rv_level/van_top.svg:www/rv_level/van_top.svg)

run() { if [[ -n $host ]]; then ssh "$host" "$1"; else bash -c "$1"; fi; }

run "test -f '$dir/configuration.yaml'" || { echo "no configuration.yaml in $dir" >&2; exit 1; }
run "cd '$dir' && mkdir -p packages custom_templates dashboards www/rv_level"
for pair in "${files[@]}"; do
  from=${pair%%:*}; to=${pair#*:}
  if [[ -n $host ]]; then scp -q "$src/$from" "$host:$dir/$to"; else cp "$src/$from" "$dir/$to"; fi
  echo "copied $to"
done

# Enable packages. Only appends when there is no homeassistant: block, so it never has to edit
# one the user wrote.
run "cd '$dir' && if grep -q '^ *packages:' configuration.yaml; then
       echo 'packages already enabled'
     elif grep -q '^homeassistant:' configuration.yaml; then
       echo 'configuration.yaml has a homeassistant: block: add this line under it, then restart:' >&2
       echo '  packages: !include_dir_named packages' >&2
       exit 1
     else
       cp configuration.yaml configuration.yaml.bak
       printf '\nhomeassistant:\n  packages: !include_dir_named packages\n' >> configuration.yaml
       echo 'enabled packages in configuration.yaml (backup: configuration.yaml.bak)'
     fi"

echo "Done. Install Mushroom from HACS if you haven't, then restart Home Assistant."
