## 🔧 Build Instructions

### Rebuild

```bash
python3 config.sh my_config.json
make -j$(nproc)
```

Expected last line:
```
g++ -o bin/champsim ... obj/btb_bbtbDi_btb.a
```

---

## Quick Sanity Check

```bash
bin/champsim \
  --warmup_instructions 1000000 \
  --simulation_instructions 10000000 \
  ~pgratz/dpc3_traces/605.mcf_s-665B.champsimtrace.xz
```

Expected output:
```
I-BTB (Instruction BTB, 1 branch/entry)  L1=512s x 6w  L2=1024s x 13w  RAS=64
```

---

## Run Experiments

```bash
mkdir -p results

sbatch job1.sh
sbatch job2.sh
sbatch job3.sh
sbatch job4.sh
sbatch job5.sh
sbatch job6.sh
```

---

## Monitor Jobs

```bash
squeue -u $USER
```

Results will appear in the `results/` directory as each job completes.
