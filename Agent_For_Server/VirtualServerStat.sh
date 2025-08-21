#!/bin/bash

hostname=$(hostname)

# Print the header
printf "%-15s %-10s %-25s %-25s %-20s\n" "VM Name" "Status" "CPU (used/cores)" "RAM (used/total GB)" "Disk (used/total GB)"

# Loop through each VM (VMID, Name, Status)
qm list | awk 'NR>1 {print $1, $2, $3}' | while read -r VMID NAME STATUS; do
    # Skip templates
    if qm config "$VMID" | grep -q "^template: 1"; then
        continue
    fi

    # Try to fetch RRD data from pvesh
    RRD_JSON=$(pvesh get /nodes/$hostname/qemu/$VMID/rrddata --timeframe hour --cf AVERAGE --output-format json 2>/dev/null)

    # Check if valid JSON
    if ! echo "$RRD_JSON" | jq empty >/dev/null 2>&1; then
        CPU_STR="?"; RAM_STR="?"; DISK_STR="?"
    else
        # Get the latest valid entry with CPU data
        LATEST=$(echo "$RRD_JSON" | jq 'reverse | map(select(.cpu != null)) | .[0]')

        if [[ "$LATEST" == "null" || -z "$LATEST" ]]; then
            CPU_STR="?"; RAM_STR="?"; DISK_STR="?"
        else
            CPU=$(echo "$LATEST" | jq '.cpu')
            MAXCPU=$(echo "$LATEST" | jq '.maxcpu')
            MEM=$(echo "$LATEST" | jq '.mem')
            MAXMEM=$(echo "$LATEST" | jq '.maxmem')

            # CPU usage
            if [[ "$CPU" != "null" && "$MAXCPU" != "null" && "$MAXCPU" != "0" ]]; then
                CPU_PCT=$(awk "BEGIN {printf \"%.2f\", $CPU * 100}")
                CPU_STR="$CPU_PCT/$MAXCPU"
            else
                CPU_STR="?"
            fi

            # RAM usage
            if [[ "$MEM" != "null" && "$MAXMEM" != "null" && "$MAXMEM" != "0" ]]; then
                USED_GB=$(awk "BEGIN {printf \"%.2f\", $MEM / (1024*1024*1024)}")
                TOTAL_GB=$(awk "BEGIN {printf \"%.2f\", $MAXMEM / (1024*1024*1024)}")
                RAM_STR="${USED_GB}/${TOTAL_GB}"
            else
                RAM_STR="?"
            fi

            # Disk usage via LVM-thin
            LV_INFO=$(lvs --noheadings -o lv_name,lv_size,data_percent | grep "vm-${VMID}-disk-0")
            if [[ -n "$LV_INFO" ]]; then
                LV_NAME=$(echo "$LV_INFO" | awk '{print $1}')
                LV_SIZE=$(echo "$LV_INFO" | awk '{print $2}')
                LV_USED_PCT=$(echo "$LV_INFO" | awk '{print $3}')

                # Strip unit (assume g)
                TOTAL_GB=$(echo "$LV_SIZE" | sed 's/[A-Za-z]//g')
                USED_GB=$(awk "BEGIN {printf \"%.2f\", $TOTAL_GB * $LV_USED_PCT / 100}")

                DISK_STR="${USED_GB}/${TOTAL_GB}"
            else
                DISK_STR="?"
            fi
        fi
    fi

    # Print the final row
    printf "%-15s %-10s %-25s %-25s %-20s\n" "$NAME" "$STATUS" "$CPU_STR" "$RAM_STR" "$DISK_STR"
done