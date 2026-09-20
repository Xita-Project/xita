"""Exact-image and exact-emission gate for one captured marker record."""
import hashlib
SPANS=((0xA1EC0,0x11c,"df35166653852048b9e681f4d447075ad5eeb44f437e0f223c484e93f606d274"),
 (0xB5B40,339,"21273987e0276cda51a2d70b2b576abc42195b8efeff9ab4e8f552a15b323726"),
 (0xB5F60,291,"9f10d4414ec5fb6b39f5f20f6100791e0aec209d77f6c60599402dff6bdb5d78"))
REGION_SHA="f3202b92e4e0a22134ee29b97530aa0275c9f9975212332d7d1ff2952ab6997f"
def matches(image):
    return all(hashlib.sha256(image.bytes_at(a,n) or b"").hexdigest()==h for a,n,h in SPANS)
def hook(body):
    start="L_000A1F5F:\n";end="    /* 000A1F9A"
    if body.count(start)!=1 or body.count(end)!=1:return body
    a=body.index(start)+len(start);b=body.index(end,a)
    if hashlib.sha256(body[a:b].encode()).hexdigest()!=REGION_SHA:return body
    return (body[:a]+"#if XV_NATIVE_MARKER_RECORD\n"
        "    { extern int xv_math_marker_record(xctx *); if (xv_math_marker_record(c)) goto L_XITA_MARKER_DONE; }\n"
        "#endif\n"+body[a:b]+"#if XV_NATIVE_MARKER_RECORD\nL_XITA_MARKER_DONE:\n#endif\n"+body[b:])
