#!/bin/bash

echo "=== X68000 Emulator fmgen Performance Test ==="
echo "Testing both original and optimized versions..."
echo

# Function to measure CPU usage of a process
measure_cpu() {
    local app_name="$1"
    local test_name="$2"
    local duration="$3"
    
    echo "--- $test_name Test ---"
    echo "Starting X68000 emulator..."
    echo "Please load a game with music and let it play"
    echo "Test duration: ${duration} seconds"
    echo "Press Enter when ready to start measurement..."
    read
    
    # Find the process ID
    local pid=$(pgrep -f "$app_name" | head -1)
    if [ -z "$pid" ]; then
        echo "Error: Could not find running X68000 process"
        return 1
    fi
    
    echo "Found process PID: $pid"
    echo "Starting measurement for ${duration} seconds..."
    
    # Create temporary file for measurements
    local temp_file="/tmp/cpu_measurement_$$.txt"
    
    # Sample CPU usage every 0.1 seconds
    local samples=$((duration * 10))
    for i in $(seq 1 $samples); do
        # Get CPU percentage using ps
        ps -p $pid -o %cpu= 2>/dev/null >> "$temp_file"
        sleep 0.1
    done
    
    if [ -s "$temp_file" ]; then
        local avg_cpu=$(awk '{sum+=$1; count++} END {print sum/count}' "$temp_file")
        local max_cpu=$(awk 'BEGIN{max=0} {if($1>max) max=$1} END{print max}' "$temp_file")
        
        echo "Average CPU Usage: ${avg_cpu}%"
        echo "Peak CPU Usage: ${max_cpu}%"
        echo "Total samples: $(wc -l < "$temp_file")"
        
        # Store results
        echo "$test_name,$avg_cpu,$max_cpu" >> "/tmp/fmgen_results.csv"
    else
        echo "Error: No CPU data collected"
    fi
    
    rm -f "$temp_file"
    echo
}

# Create results file
echo "Test,Average_CPU,Peak_CPU" > "/tmp/fmgen_results.csv"

# Test 1: Original implementation (Level 0)
echo "First, we'll test the original implementation..."
echo "Please modify fmgen_config.h to set FMGEN_OPTIMIZATION_LEVEL = 0"
echo "Then rebuild and run the emulator."
echo "Press Enter when done..."
read

measure_cpu "X68000" "Original (Level 0)" 30

echo "Now we'll test the optimized implementation..."
echo "Please modify fmgen_config.h to set FMGEN_OPTIMIZATION_LEVEL = 1"  
echo "Then rebuild and run the emulator."
echo "Press Enter when done..."
read

measure_cpu "X68000" "Optimized (Level 1)" 30

# Display results
echo "=== Final Results ==="
echo
cat "/tmp/fmgen_results.csv" | column -t -s ','
echo

# Calculate improvement
if [ -f "/tmp/fmgen_results.csv" ]; then
    # Extract CPU values (skip header)
    original_cpu=$(sed -n '2p' "/tmp/fmgen_results.csv" | cut -d',' -f2)
    optimized_cpu=$(sed -n '3p' "/tmp/fmgen_results.csv" | cut -d',' -f2)
    
    if [ -n "$original_cpu" ] && [ -n "$optimized_cpu" ]; then
        improvement=$(echo "scale=2; ($original_cpu - $optimized_cpu) / $original_cpu * 100" | bc -l)
        echo "CPU Usage Improvement: ${improvement}%"
        
        if (( $(echo "$improvement > 0" | bc -l) )); then
            echo "✅ Optimization reduced CPU usage"
        else
            echo "⚠️  Optimization increased CPU usage"
        fi
    fi
fi

echo
echo "Results saved to: /tmp/fmgen_results.csv"
echo "For more detailed profiling, use:"
echo "  instruments -t 'Time Profiler' /path/to/X68000.app"