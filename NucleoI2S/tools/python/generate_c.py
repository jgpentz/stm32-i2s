from packets import generate_header
from pathlib import Path

from sauron.smartcard_interface import protocol
from sauron.smartcard_interface.protocol import Header

if __name__ == "__main__":
    OUTPUT_DIR = Path(__file__).parent.parent.parent / "app/src/"

    generate_header(
        protocol,
        dest=OUTPUT_DIR / "protocol.h",
        base_packet=Header,
        one_module=False,
        uncrustify=False,
    )
