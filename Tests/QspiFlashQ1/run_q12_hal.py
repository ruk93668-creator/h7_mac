"""Keep Q1.1 evidence separate; verify new APIs link against the real HAL."""
from pathlib import Path
here = Path(__file__).parent
source = (here / 'run_hal_link.py').read_text(encoding='utf-8')
source = source.replace("'output/QspiFlashQ1/hal_link'", "'output/QspiFlashQ1/q12/hal_link'")
source = source.replace("'BSP_QSPI_Init', 'HAL_QSPI_Init'", "'BSP_QSPI_Init', 'BSP_QSPI_Read', 'BSP_QSPI_ReadStatus', 'HAL_QSPI_Init'")
exec(compile(source, str(here / 'run_hal_link.py'), 'exec'))
