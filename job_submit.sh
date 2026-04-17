#!/bin/bash
#SBATCH --job-name=serial_job_test   # Job name
#SBATCH --mail-type=END,FAIL         # Mail Events (NONE,BEGIN,FAIL,END,ALL)
#SBATCH --mail-user=dhanya18tamu.edu   # Replace with your email address
#SBATCH --ntasks=1                   # Run on a single CPU
#SBATCH --time=12:00:00              # Time limit hh:mm:ss
#SBATCH --output=serial_test_%j.log  # Standard output and error log
#SBATCH --qos=olympus-academic       # Do not change
#SBATCH --partition=adademic         # Do not change

echo "Running 602.gcc_s-734B.champsimtrace.xz"
bin/champsim \
  --warmup_instructions 200000000 \
  --simulation_instructions 1000000000 \
  ~pgratz/dpc3_traces/602.gcc_s-734B.champsimtrace.xz \
  > 602.gcc_s-734B.champsimtrace.txt

### Add more ChampSim runs below.

