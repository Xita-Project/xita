# xita.dev — static site

Plain HTML/CSS, no build step: `index.html` + `style.css` + `img/`. `CNAME` holds the custom domain.

Hosting: GitHub Pages serving this directory (or a copy of it in a public `xita-site` repository while
the main repository is private; Pages needs a paid plan to publish from a private repo).

DNS at the registrar for `xita.dev`:
- A records on the apex: 185.199.108.153, 185.199.109.153, 185.199.110.153, 185.199.111.153
- AAAA (optional): 2606:50c0:8000::153, 2606:50c0:8001::153, 2606:50c0:8002::153, 2606:50c0:8003::153
- CNAME `www` -> `birchwoodgod.github.io`
Then in the repository's Pages settings set the custom domain to `xita.dev` and enable "Enforce HTTPS".
`.dev` is on the HSTS preload list, so the site only works over HTTPS; Pages issues the certificate
once the DNS records resolve (allow up to an hour).

Rules: never commit or host game data (XBE, maps, halo_image.bin). Screenshots and clips only.
