#!/bin/bash

echo "=== ADPCM Optimization Test ==="
echo

# Function to check current optimization level
check_optimization_level() {
    local adpcm_h="X68000 Shared/px68k/x68k/adpcm_optimized.h"
    
    if [ ! -f "$adpcm_h" ]; then
        echo "❌ adpcm_optimized.h not found"
        return 1
    fi
    
    local enabled=$(grep "ADPCM_ENABLE_OPTIMIZATIONS" "$adpcm_h" | grep -o "[01]")
    local level=$(grep "ADPCM_OPTIMIZATION_LEVEL" "$adpcm_h" | grep -o "[0-9]")
    
    echo "📊 Current Configuration:"
    echo "   ADPCM_ENABLE_OPTIMIZATIONS: $enabled"
    echo "   ADPCM_OPTIMIZATION_LEVEL: $level"
    
    if [ "$enabled" = "1" ]; then
        echo "   Status: ✅ Optimizations ENABLED"
        case "$level" in
            0) echo "   Mode: Original code" ;;
            1) echo "   Mode: Safe optimizations (pow() removal, branchless)" ;;
            2) echo "   Mode: Advanced optimizations" ;;
            *) echo "   Mode: Unknown level" ;;
        esac
    else
        echo "   Status: ❌ Optimizations DISABLED"
    fi
    
    return 0
}

# Function to measure performance
measure_adpcm_performance() {
    local test_name="$1"
    echo "--- $test_name Performance Test ---"
    
    # Check if emulator is running
    local pid=$(pgrep -f "X68000" | head -1)
    if [ -z "$pid" ]; then
        echo "⚠️  X68000 emulator is not running"
        echo "   Please start the emulator and load a game with ADPCM audio"
        echo "   (Examples: many X68000 games use ADPCM for voice/sound effects)"
        return 1
    fi
    
    echo "Found X68000 process: PID $pid"
    
    # Measure CPU usage during ADPCM playback
    echo "Measuring CPU usage for 10 seconds..."
    local cpu_samples=()
    local memory_samples=()
    
    for i in {1..100}; do
        local cpu=$(ps -p $pid -o %cpu= 2>/dev/null | tr -d ' ')
        local mem=$(ps -p $pid -o rss= 2>/dev/null | tr -d ' ')
        
        if [ -n "$cpu" ] && [ -n "$mem" ]; then
            cpu_samples+=($cpu)
            memory_samples+=($mem)
        fi
        
        sleep 0.1
    done
    
    # Calculate averages
    local cpu_sum=0
    local mem_sum=0
    local sample_count=${#cpu_samples[@]}
    
    if [ $sample_count -gt 0 ]; then
        for cpu in "${cpu_samples[@]}"; do
            cpu_sum=$(echo "$cpu_sum + $cpu" | bc -l 2>/dev/null || echo $cpu_sum)
        done
        
        for mem in "${memory_samples[@]}"; do
            mem_sum=$((mem_sum + mem))
        done
        
        local avg_cpu=$(echo "scale=2; $cpu_sum / $sample_count" | bc -l 2>/dev/null || echo "N/A")
        local avg_mem=$((mem_sum / sample_count))
        local avg_mem_mb=$(echo "scale=1; $avg_mem / 1024" | bc -l 2>/dev/null || echo "N/A")
        
        echo "📊 Results:"
        echo "   Average CPU: ${avg_cpu}%"
        echo "   Average Memory: ${avg_mem} KB (${avg_mem_mb} MB)"
        echo "   Sample Count: $sample_count"
        
        # Store results for comparison
        echo "$test_name,$avg_cpu,$avg_mem" >> "/tmp/adpcm_test_results.csv"
    else
        echo "❌ No performance data collected"
    fi
    
    echo
}

# Function to test different optimization levels
test_optimization_levels() {
    echo "=== Optimization Level Comparison ==="
    echo
    
    local adpcm_h="X68000 Shared/px68k/x68k/adpcm_optimized.h"
    
    # Test Level 0 (Original)
    echo "Setting optimization to Level 0 (Original)..."
    sed -i '' 's/ADPCM_ENABLE_OPTIMIZATIONS [01]/ADPCM_ENABLE_OPTIMIZATIONS 0/' "$adpcm_h"
    sed -i '' 's/ADPCM_OPTIMIZATION_LEVEL [0-9]/ADPCM_OPTIMIZATION_LEVEL 0/' "$adpcm_h"
    
    echo "Please rebuild the project with Level 0 and test..."
    echo "Press Enter when ready to measure Level 0 performance..."
    read
    measure_adpcm_performance "Level 0 (Original)"
    
    # Test Level 1 (Safe optimizations)  
    echo "Setting optimization to Level 1 (Safe)..."
    sed -i '' 's/ADPCM_ENABLE_OPTIMIZATIONS [01]/ADPCM_ENABLE_OPTIMIZATIONS 1/' "$adpcm_h"
    sed -i '' 's/ADPCM_OPTIMIZATION_LEVEL [0-9]/ADPCM_OPTIMIZATION_LEVEL 1/' "$adpcm_h"
    
    echo "Please rebuild the project with Level 1 and test..."
    echo "Press Enter when ready to measure Level 1 performance..."
    read
    measure_adpcm_performance "Level 1 (Safe Optimizations)"
}

# Function to analyze results
analyze_results() {
    local results_file="/tmp/adpcm_test_results.csv"
    
    if [ ! -f "$results_file" ]; then
        echo "❌ No test results found"
        return 1
    fi
    
    echo "=== Performance Analysis ==="
    echo
    echo "📊 Results Summary:"
    cat "$results_file" | while IFS=',' read -r test_name cpu memory; do
        echo "   $test_name: CPU=${cpu}%, Memory=${memory}KB"
    done
    
    # Compare if we have both results
    local level0_cpu=$(grep "Level 0" "$results_file" | cut -d',' -f2)
    local level1_cpu=$(grep "Level 1" "$results_file" | cut -d',' -f2)
    
    if [ -n "$level0_cpu" ] && [ -n "$level1_cpu" ]; then
        echo
        echo "🔍 Comparison:"
        local improvement=$(echo "scale=2; ($level0_cpu - $level1_cpu) / $level0_cpu * 100" | bc -l 2>/dev/null)
        
        if [ -n "$improvement" ]; then
            echo "   CPU Usage Improvement: ${improvement}%"
            
            if (( $(echo "$improvement > 0" | bc -l 2>/dev/null) )); then
                echo "   ✅ Optimization is effective!"
            else
                echo "   ⚠️  No significant improvement detected"
            fi
        fi
    fi
}

# Main execution
main() {
    check_optimization_level
    echo
    
    echo "Choose test mode:"
    echo "1. Single performance test (current settings)"
    echo "2. Compare optimization levels (requires rebuilds)"
    echo "3. Analyze previous results"
    echo
    read -p "Enter choice (1-3): " choice
    
    case $choice in
        1)
            echo
            measure_adpcm_performance "Current Settings"
            ;;
        2)
            test_optimization_levels
            analyze_results
            ;;
        3)
            analyze_results
            ;;
        *)
            echo "Invalid choice"
            exit 1
            ;;
    esac
    
    echo
    echo "=== Test Complete ==="
    echo "Results saved to: /tmp/adpcm_test_results.csv"
}

# Check for bc (calculator)
if ! command -v bc &> /dev/null; then
    echo "Installing bc calculator..."
    if command -v brew &> /dev/null; then
        brew install bc
    else
        echo "❌ Please install 'bc': brew install bc"
        exit 1
    fi
fi

# Initialize results file
echo "Test,CPU_Percent,Memory_KB" > "/tmp/adpcm_test_results.csv"

main "$@"