# ArduPilot Project

[![Discord](https://img.shields.io/discord/674039678562861068.svg)](https://ardupilot.org/discord)

[![Test Copter](https://github.com/ArduPilot/ardupilot/workflows/test%20copter/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_copter.yml) [![Test Plane](https://github.com/ArduPilot/ardupilot/workflows/test%20plane/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_plane.yml) [![Test Rover](https://github.com/ArduPilot/ardupilot/workflows/test%20rover/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_rover.yml) [![Test Sub](https://github.com/ArduPilot/ardupilot/workflows/test%20sub/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_sub.yml) [![Test Tracker](https://github.com/ArduPilot/ardupilot/workflows/test%20tracker/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_tracker.yml)

[![Test AP_Periph](https://github.com/ArduPilot/ardupilot/workflows/test%20ap_periph/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_sitl_periph.yml) [![Test Chibios](https://github.com/ArduPilot/ardupilot/workflows/test%20chibios/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_chibios.yml) [![Test Linux SBC](https://github.com/ArduPilot/ardupilot/workflows/test%20Linux%20SBC/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_linux_sbc.yml) [![Test Replay](https://github.com/ArduPilot/ardupilot/workflows/test%20replay/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_replay.yml)

[![Test Unit Tests](https://github.com/ArduPilot/ardupilot/workflows/test%20unit%20tests%20and%20sitl%20building/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_unit_tests.yml)[![test size](https://github.com/ArduPilot/ardupilot/actions/workflows/test_size.yml/badge.svg)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_size.yml)

[![Test Environment Setup](https://github.com/ArduPilot/ardupilot/actions/workflows/test_environment.yml/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_environment.yml)

[![Cygwin Build](https://github.com/ArduPilot/ardupilot/actions/workflows/cygwin_build.yml/badge.svg)](https://github.com/ArduPilot/ardupilot/actions/workflows/cygwin_build.yml) [![Macos Build](https://github.com/ArduPilot/ardupilot/actions/workflows/macos_build.yml/badge.svg)](https://github.com/ArduPilot/ardupilot/actions/workflows/macos_build.yml)

[![Coverity Scan Build Status](https://scan.coverity.com/projects/5331/badge.svg)](https://scan.coverity.com/projects/ardupilot-ardupilot)

[![Test Coverage](https://github.com/ArduPilot/ardupilot/actions/workflows/test_coverage.yml/badge.svg?branch=master)](https://github.com/ArduPilot/ardupilot/actions/workflows/test_coverage.yml)

[![Autotest Status](https://autotest.ardupilot.org/autotest-badge.svg)](https://autotest.ardupilot.org/)

[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/10598/badge)](https://www.bestpractices.dev/projects/10598)

ArduPilot is the most advanced, full-featured, and reliable open source autopilot software available.
It has been under development since 2010 by a diverse team of professional engineers, computer scientists, and community contributors.
Our autopilot software is capable of controlling almost any vehicle system imaginable, from conventional airplanes, quad planes, multi-rotors, and helicopters to rovers, boats, balance bots, and even submarines.
It is continually being expanded to provide support for new emerging vehicle types.

> **This fork** adds a `DDS_MAV_MODE` parameter to `AP_DDS` that mirrors the raw MAVLink stream
> over two DDS topics (`/vehicle_data/from_dds`, `/vehicle_data/to_dds`), for bridging a vehicle
> to a DDS-based network (e.g. via [DDS-Router](https://github.com/eProsima/DDS-Router)) without
> writing a per-message ROS 2 topic mapping. See
> [`libraries/AP_DDS/README.md`](libraries/AP_DDS/README.md#mavlink-over-dds-mirroring-mode-dds_mav_mode)
> for build/parameter/topic details.

#### `DDS_MAV_MODE` vs. MAVROS

| Axis | MAVROS | `DDS_MAV_MODE` |
|---|---|---|
| Message coverage | Only messages MAVROS has mapped (hundreds, but finite) | Every MAVLink message on the channel is included automatically; no code change needed for new message types |
| Schema / type safety | Type-safe via DDS IDL | DDS just moves bytes -- all parsing responsibility shifts to the consumer (DroneDataviewer, MngData, etc.) |
| ROS 2 dependency | Requires a full ROS 2 stack (companion computer) | No ROS 2 needed anywhere from FC→Agent→DDS-Router→subscriber (MngData optionally uses a ROS 2 hook) |
| Domain bridging | Out of scope (assumes same domain) | DDS-Router treats domain 10 (vehicle) / domain 0 (ground) separation as a first-class concern |
| FC-side overhead | None (a separate process on the companion computer handles it) | Very light -- just memcpy's the bytes it was already sending into a queue |
| Where maintenance lives | Depends on upstream MAVROS mappings (centralized, well-tested) | Owned by the project itself -- bears its own risk, e.g. the framing bug found and fixed here |
| Security | A plain MAVLink link is usually unprotected | Can directly reuse AP_DDS's DTLS/mutual-TLS |

MAVROS is the better fit when you're already committed to living inside the ROS 2 ecosystem --
it plugs straight into rviz, nav2, MoveIt, etc. But the actual consumers of this mode (Mission
Planner-style GCS, custom viewers) are existing desktop apps with their own MAVLink parsers
already, not ROS nodes. Rather than turning those apps into ROS nodes or upstreaming support
for messages MAVROS hasn't mapped, it's more efficient to tunnel the raw MAVLink bytes over DDS
and reuse each app's existing parser.

#### `DDS_MAV_MODE` vs. MAVROS (한국어)

| 축 | MAVROS | `DDS_MAV_MODE` |
|---|---|---|
| 메시지 커버리지 | MAVROS가 매핑해둔 메시지만 (수백 개지만 유한) | 채널에 실리는 모든 MAVLink 메시지 자동 포함, 신규 메시지 추가 시 코드 변경 불필요 |
| 스키마/타입세이프티 | DDS 레벨에서 IDL로 타입 보장 | DDS는 바이트만 옮김 -- 파싱 책임이 전부 컨슈머(DroneDataviewer, MngData 등)로 이전 |
| ROS2 의존성 | 풀 ROS2 스택 필수 (컴패니언 컴퓨터) | FC→Agent→DDS-Router→구독자 전 구간 ROS2 불필요 (MngData만 선택적으로 ROS2 훅 사용) |
| 도메인 브리징 | 관심사 밖 (동일 도메인 가정) | DDS-Router가 도메인10(드론)/도메인0(관제) 분리를 1급 시민으로 처리 |
| FC 측 오버헤드 | 없음(별도 프로세스가 companion에서 처리) | 매우 가벼움 -- 기존에 나가던 바이트를 memcpy해서 큐에 넣을 뿐 |
| 유지보수 소재지 | 업스트림 MAVROS 매핑에 의존 (중앙집중, 검증됨) | 프로젝트가 직접 소유 -- 프레이밍 버그 같은 리스크를 자체 부담 |
| 보안 | 표준 MAVLink 링크는 보통 무보호 | AP_DDS의 DTLS/mutual-TLS를 그대로 재사용 가능 |

MAVROS는 "이미 ROS2 생태계 안에서 살 것"이 전제일 때 유리하다 -- rviz, nav2, MoveIt 등과
바로 붙는다. 하지만 이 모드를 쓰는 실제 소비자들(Mission Planner류 GCS, 커스텀 뷰어)은
전부 이미 MAVLink 파서를 가진 기존 데스크톱 앱이지 ROS 노드가 아니다. 그 앱들을 ROS
노드로 새로 만들거나 MAVROS 매핑에 없는 메시지를 위해 업스트림에 기여하는 것보다, 원본
MAVLink를 그대로 DDS로 실어 나르고 각 앱의 기존 파서를 재사용하는 쪽이 개발량 대비
합리적이다.

## The ArduPilot project is made up of

- ArduCopter: [code](https://github.com/ArduPilot/ardupilot/tree/master/ArduCopter), [wiki](https://ardupilot.org/copter/index.html)

- ArduPlane: [code](https://github.com/ArduPilot/ardupilot/tree/master/ArduPlane), [wiki](https://ardupilot.org/plane/index.html)

- Rover: [code](https://github.com/ArduPilot/ardupilot/tree/master/Rover), [wiki](https://ardupilot.org/rover/index.html)

- ArduSub : [code](https://github.com/ArduPilot/ardupilot/tree/master/ArduSub), [wiki](http://ardusub.com/)

- Antenna Tracker : [code](https://github.com/ArduPilot/ardupilot/tree/master/AntennaTracker), [wiki](https://ardupilot.org/antennatracker/index.html)

## User Support & Discussion Forums

- Support Forum: <https://discuss.ardupilot.org/>

- Community Site: <https://ardupilot.org>

## Developer Information

- Github repository: <https://github.com/ArduPilot/ardupilot>

- Main developer wiki: <https://ardupilot.org/dev/>

- Developer discussion: <https://discuss.ardupilot.org>

- Developer chat: <https://discord.com/channels/ardupilot>

## Top Contributors

- [Flight code contributors](https://github.com/ArduPilot/ardupilot/graphs/contributors)
- [Wiki contributors](https://github.com/ArduPilot/ardupilot_wiki/graphs/contributors)
- [Most active support forum users](https://discuss.ardupilot.org/u?order=post_count&period=quarterly)
- [Partners who contribute financially](https://ardupilot.org/about/Partners)

## How To Get Involved

- The ArduPilot project is open source and we encourage participation and code contributions: [guidelines for contributors to the ardupilot codebase](https://ardupilot.org/dev/docs/contributing.html)

- We have an active group of Beta Testers to help us improve our code: [release procedures](https://ardupilot.org/dev/docs/release-procedures.html)

- Desired Enhancements and Bugs can be posted to the [issues list](https://github.com/ArduPilot/ardupilot/issues).

- Help other users with log analysis in the [support forums](https://discuss.ardupilot.org/)

- Improve the wiki and chat with other [wiki editors on Discord #documentation](https://discord.com/channels/ardupilot)

- Contact the developers on one of the [communication channels](https://ardupilot.org/copter/docs/common-contact-us.html)

## License

The ArduPilot project is licensed under the GNU General Public
License, version 3.

- [Overview of license](https://ardupilot.org/dev/docs/license-gplv3.html)

- [Full Text](https://github.com/ArduPilot/ardupilot/blob/master/COPYING.txt)

## Maintainers

ArduPilot is comprised of several parts, vehicles and boards. The list below
contains the people that regularly contribute to the project and are responsible
for reviewing patches on their specific area.

- [Andrew Tridgell](https://github.com/tridge):
  - ***Vehicle***: Plane, AntennaTracker
  - ***Board***: Pixhawk, Pixhawk2, PixRacer
- [Francisco Ferreira](https://github.com/oxinarf):
  - ***Bug Master***
- [Grant Morphett](https://github.com/gmorph):
  - ***Vehicle***: Rover
- [Willian Galvani](https://github.com/williangalvani):
  - ***Vehicle***: Sub
  - ***Board***: Navigator
- [Michael du Breuil](https://github.com/WickedShell):
  - ***Subsystem***: Batteries
  - ***Subsystem***: GPS
  - ***Subsystem***: Scripting
- [Peter Barker](https://github.com/peterbarker):
  - ***Subsystem***: DataFlash, Tools
- [Randy Mackay](https://github.com/rmackay9):
  - ***Vehicle***: Copter, Rover, AntennaTracker
- [Siddharth Purohit](https://github.com/bugobliterator):
  - ***Subsystem***: CAN, Compass
  - ***Board***: Cube*
- [Tom Pittenger](https://github.com/magicrub):
  - ***Vehicle***: Plane
- [Bill Geyer](https://github.com/bnsgeyer):
  - ***Vehicle***: TradHeli
- [Emile Castelnuovo](https://github.com/emilecastelnuovo):
  - ***Board***: VRBrain
- [Georgii Staroselskii](https://github.com/staroselskii):
  - ***Board***: NavIO
- [Gustavo José de Sousa](https://github.com/guludo):
  - ***Subsystem***: Build system
- [Julien Beraud](https://github.com/jberaud):
  - ***Board***: Bebop & Bebop 2
- [Leonard Hall](https://github.com/lthall):
  - ***Subsystem***: Copter attitude control and navigation
- [Matt Lawrence](https://github.com/Pedals2Paddles):
  - ***Vehicle***: 3DR Solo & Solo based vehicles
- [Matthias Badaire](https://github.com/badzz):
  - ***Subsystem***: FRSky
- [Mirko Denecke](https://github.com/mirkix):
  - ***Board***: BBBmini, BeagleBone Blue, PocketPilot
- [Paul Riseborough](https://github.com/priseborough):
  - ***Subsystem***: AP_NavEKF2
  - ***Subsystem***: AP_NavEKF3
- [Víctor Mayoral Vilches](https://github.com/vmayoral):
  - ***Board***: PXF, Erle-Brain 2, PXFmini
- [Amilcar Lucas](https://github.com/amilcarlucas):
  - ***Subsystem***: Marvelmind
- [Samuel Tabor](https://github.com/samuelctabor):
  - ***Subsystem***: Soaring/Gliding
- [Henry Wurzburg](https://github.com/Hwurzburg):
  - ***Subsystem***: OSD
  - ***Site***: Wiki
- [Peter Hall](https://github.com/IamPete1):
  - ***Vehicle***: Tailsitters
  - ***Vehicle***: Sailboat
  - ***Subsystem***: Scripting
- [Andy Piper](https://github.com/andyp1per):
  - ***Subsystem***: Crossfire
  - ***Subsystem***: ESC
  - ***Subsystem***: OSD
  - ***Subsystem***: SmartAudio
- [Alessandro Apostoli](https://github.com/yaapu):
  - ***Subsystem***: Telemetry
  - ***Subsystem***: OSD
- [Rishabh Singh](https://github.com/rishabsingh3003):
  - ***Subsystem***: Avoidance/Proximity
- [David Bussenschutt](https://github.com/davidbuzz):
  - ***Subsystem***: ESP32,AP_HAL_ESP32
- [Charles Villard](https://github.com/Silvanosky):
  - ***Subsystem***: ESP32,AP_HAL_ESP32
