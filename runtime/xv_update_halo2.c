/* Reuse the verified transaction implementation, with separate static state,
 * fixed filenames and symbols. No path is accepted from a network request. */
#define XV_UPDATE_HALO2 1
#define xv_update_init xv_halo2_update_init
#define xv_update_begin xv_halo2_update_begin
#define xv_update_chunk xv_halo2_update_chunk
#define xv_update_finish xv_halo2_update_finish
#define xv_update_close xv_halo2_update_close
#define xv_update_request xv_halo2_update_request
#define xv_update_requested xv_halo2_update_requested
#define xv_update_status xv_halo2_update_status
#define xv_update_json xv_halo2_update_json
#define xv_update_boot xv_halo2_update_boot
#define xv_update_confirm xv_halo2_update_confirm
#include "xv_update.c"
