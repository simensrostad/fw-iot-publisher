# IoT Publisher

To build and run:
 1. Make a new folder with a suitable name.
 2. Access the folder and clone the repository.
 3. Execute ``west init -l`` and ``west update``. The project dependencies will build outside the ``nrf_publisher`` repository.
 4. Execute ``west build -b <board_name>``
 5. Execute ``west flash`` to flash the firmware to the board.
 