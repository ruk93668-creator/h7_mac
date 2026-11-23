"""Use the original historical-suite comparison against the Q1.3 snapshots."""
from pathlib import Path

harness = Path(__file__).resolve().with_name('run_q12_regression.py')
source = harness.read_text(encoding='utf-8').replace("'output/QspiFlashQ1/q12'", "'output/QspiFlashQ1/q13'")
exec(compile(source, str(harness), 'exec'))
