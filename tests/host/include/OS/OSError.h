#pragma once

// Stand-in (PC build): routes OSReport (TM_LOG in stub sources) into host::log.
void OSReport(const char* format, ...);
