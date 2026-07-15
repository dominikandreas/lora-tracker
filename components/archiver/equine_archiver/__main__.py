from __future__ import annotations

import logging
import os

from .config import ArchiverConfig
from .service import ArchiverService, make_mqtt_client


def main() -> None:
    logging.basicConfig(
        level=os.getenv("LOG_LEVEL", "INFO").upper(),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    config = ArchiverConfig.from_env()
    ArchiverService(config, make_mqtt_client(config)).run()


if __name__ == "__main__":
    main()
