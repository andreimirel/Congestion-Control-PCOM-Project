# Congestion-Control-PCOM-ProjectT

# Congestion Control Project (PCom)

## Overview
This project implements a congestion control algorithm aimed at maximizing performance and fairness while minimizing per-packet latency. The implementation is built within the HTsim simulator, modifying the `cc.cpp` file, specifically the functions `CCSrc::processAck()` and `CCSrc::processNack()`.

## Implementation Details
The provided code implements a congestion control mechanism using the Cubic algorithm. The key features of the implementation include:
- **Congestion Window Management**: The congestion window (`_cwnd`) is adjusted based on ACK and NACK reception.
- **Cubic Window Growth**: Uses a cubic function to calculate the congestion window increase over time.
- **Explicit Congestion Notification (ECN) Support**: ECN-marked packets help adjust congestion control behavior.
- **Slow Start and Congestion Avoidance**: Implements a transition from slow start to congestion avoidance when necessary.
- **Packet Handling**: Implements logic for sending packets, handling acknowledgments, and processing congestion events.

## Key Variables
- `_cwnd`: Congestion window size
- `_ssthresh`: Slow-start threshold
- `_flightsize`: Number of bytes in flight
- `_mss`: Maximum Segment Size
- `epochStartTime`: Start time of the current congestion epoch
- `lastCongestionOrigin`: Window size before last congestion event
- `minRoundTripTime`: Minimum observed round-trip time

## Key Functions
### `CCSrc::processAck(const CCAck& ack)`
Handles ACK reception and adjusts `_cwnd` based on Cubic calculations.

### `CCSrc::processNack(const CCNack& nack)`
Handles packet loss (NACK reception) and reduces `_cwnd` accordingly.

### `CCSrc::send_packet()`
Handles sending packets and adjusting the number of bytes in flight.

### `CCSrc::receivePacket(Packet& pkt)`
Processes incoming packets, deciding whether they are ACKs or NACKs.

### `CCSink::receivePacket(Packet& pkt)`
Handles packet reception at the sink and sends ACKs/NACKs accordingly.
