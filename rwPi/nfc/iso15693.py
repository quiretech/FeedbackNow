"""
ISO15693 high-level API: get_uid, read_block, write_block.
Uses PN5180 driver; UID is always 8 bytes MSB-first; block size 4 bytes.
"""
from typing import Optional, Tuple

from nfc.pn5180 import PN5180, PN5180_OK, strerror

BLOCK_SIZE = 4


def get_uid(device: PN5180) -> Tuple[int, bytes]:
    """
    Perform inventory and return (err, uid_8_bytes).
    UID is MSB-first hex string friendly.
    """
    return device.get_inventory()


def read_block(
    device: PN5180, uid: bytes, block_num: int, block_size: int = BLOCK_SIZE
) -> Tuple[int, bytes]:
    """
    Read one block from tag. Returns (err, data_4bytes).
    """
    return device.read_block(uid, block_num, block_size)


def write_block(
    device: PN5180,
    uid: bytes,
    block_num: int,
    data: bytes,
    block_size: Optional[int] = None,
) -> int:
    """
    Write 4 bytes to one block. data must be at least 4 bytes.
    """
    if block_size is None:
        block_size = min(len(data), BLOCK_SIZE)
    return device.write_block(uid, block_num, data, block_size)
