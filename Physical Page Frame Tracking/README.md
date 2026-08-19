# Physical Page Frame Tracking in xv6

## 한 줄 요약
xv6의 free-list 기반 물리 메모리 할당기에 프레임 단위 실시간 사용 현황 추적 기능과, 소프트웨어 기반 주소 변환 및 역페이지 테이블(IPT)을 추가한 과제.

## 구현한 내용
### 필수 구현 (A, B)
- 전역 프레임 정보 테이블 `pf_info[PFNNUM]` (`PFNNUM = 60000`)을 커널에 도입, 각 엔트리는 `frame_index`, `allocated`, `pid`, `start_tick`을 보관
- `kalloc()`/`kfree()`를 수정해 프레임 할당·해제 시점에 테이블을 갱신
  - `kalloc()`: 할당된 프레임의 `allocated = 1`, `pid = 현재 프로세스 PID`, `start_tick = ticks`로 기록 (커널 전용 페이지는 추적 제외)
  - `kfree()`: 해제되는 프레임의 `allocated = 0`, `pid = -1`, `start_tick = 0`으로 초기화
  - `kmem.lock` 스핀락을 재사용해 갱신 구간을 임계구역으로 보호
- 시스템 콜 `dump_physmem_info(void *addr, int max_entries)`를 추가해 커널의 프레임 테이블을 `copyout()`으로 유저 버퍼에 구조체 배열 형태로 복사, 복사된 엔트리 수를 반환
- 검증용 유저 프로그램 `memdump.c`(할당된/전체 프레임 출력, `-a` 전체 출력, `-p <PID>` 필터), `memstress.c`(`-n`/`-t`/`-w` 옵션으로 페이지 확보 및 접근을 유발), `memtest.c`(위 둘을 조합한 통합 테스트)를 작성해 `Makefile`의 `UPROGS`에 추가

### 추가 구현 (C)
- `sw_vtop(pde_t *pgdir, const void *va, uint *pa_out, uint *pte_flags_out)`: 하드웨어 페이지워크 없이 소프트웨어만으로 PDE/PTE 인덱스를 계산하고 present/권한 비트를 확인해 가상주소→물리주소 변환을 수행
- 역페이지 테이블(IPT, `ipt.h`)을 해시 체인(`struct ipt_entry { pfn, pid, va, flags, refcnt, next }`)으로 구현하고 `mappages()` 등 매핑/해제 경로에서 삽입·삭제를 동기화 (`ipt_init`, `ipt_insert`, `ipt_remove`, `ipt_remove_pfn`, `ipt_lookup_pfn`)
- 사용자 인터페이스 프로그램 `vtop.c`(가상주소→물리주소 조회), `pfind.c`(물리 프레임번호로 역방향 참조 프로세스 조회), `test_c.c`(추가 기능 검증용 테스트)를 작성

## 사용 스택
- C (xv6 커널 및 유저 프로그램)
- x86 어셈블리 (xv6 부트/트랩 관련 기존 코드), x86 페이지 테이블(PDE/PTE) 구조 직접 파싱
- xv6-public (MIT PDOS) 기반 커널, `qemu`/`bochs` 에뮬레이터 상에서 빌드 및 실행
- GNU Make
