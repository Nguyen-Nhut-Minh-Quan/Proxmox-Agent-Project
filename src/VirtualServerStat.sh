#!/bin/bash
# Header
printf "%-6s %-25s %-10s %-20s %-20s %-25s\n" \
"VMID" "Name" "Status" "CPU (used/cores)" "RAM (used/total MB)" "Disk (used/total GB)"

# Loop over all VMs
while read -r vmid name status mem_alloc disk_alloc pid; do

  # Get VM config
  config=$(qm config "$vmid")

  # Get vCPU count (default 1 if missing)
  cpu_total=$(echo "$config" | grep -Po '^cores:\s*\K\d+' || echo 1)

  # Get RAM total from config (fallback to mem_alloc)
  ram_total=$(echo "$config" | grep -Po '^memory:\s*\K\d+')
  ram_total=${ram_total:-$mem_alloc}

  # Get PID if running
  if [[ "$status" == "running" && "$pid" != "0" ]]; then
    cpu_used=$(ps -p "$pid" -o %cpu= | awk '{printf "%.2f", $1}')
    ram_used=$(ps -p "$pid" -o rss= | awk '{printf "%d", $1 / 1024}')

    # Cap RAM used to RAM total
    if (( ram_used > ram_total )); then
      ram_used=$ram_total
    fi
  else
    cpu_used="0.00"
    ram_used="0"
  fi

  # Get LVM disk usage
  lv_name="vm-${vmid}-disk-0"
  lv_info=$(lvs --units g --noheadings -o lv_name,data_percent,lv_size | grep "$lv_name")
  if [[ -n "$lv_info" ]]; then
    data_pct=$(echo "$lv_info" | awk '{print $2}')
    total_size=$(echo "$lv_info" | awk '{print $3}' | sed 's/g//')
    disk_used=$(awk -v p="$data_pct" -v s="$total_size" 'BEGIN { printf "%.2f", (p / 100) * s }')
  else
    disk_used="0.00"
    total_size="$disk_alloc"
  fi

  # Print info row
  printf "%-6s %-25s %-10s %-20s %-20s %-25s\n" \
    "$vmid" "$name" "$status" \
    "$cpu_used/$cpu_total" "$ram_used/$ram_total" "$disk_used/$total_size"

done < <(qm list | awk 'NR>1 {printf "%s %s %s %s %s %s\n", $1, $2, $3, $4, $5, $6}')