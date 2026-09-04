# xita.dev — static site

Plain HTML/CSS, no build step: `index.html` + `style.css` + `img/`. The `CNAME` file is only read by
GitHub Pages and is harmless elsewhere.

## Hosting (Cloudflare Pages or Netlify, connected to the private repository)

Both hosts can read a private GitHub repository through their GitHub app; grant access to `xita` only.

Cloudflare Pages:
1. Workers & Pages -> Create -> Pages -> Connect to Git -> `BirchWoodGod/xita`, branch `main`.
2. Build settings: framework `None`, build command empty, build output directory `site`.
3. Custom domains -> add `xita.dev` (and `www.xita.dev`). If the domain's DNS is on Cloudflare the
   records are created for you; otherwise add a CNAME for the apex (with CNAME flattening) and for
   `www` pointing at `<project>.pages.dev`.

Netlify:
1. Add new site -> Import from Git -> `BirchWoodGod/xita`, branch `main`.
2. Build settings: build command empty, publish directory `site`.
3. Domain management -> add `xita.dev`. Either move DNS to Netlify DNS, or at the registrar set the
   apex A record to `75.2.60.5` and CNAME `www` to `<site>.netlify.app`.

`.dev` is on the browser HSTS preload list, so the site only works over HTTPS; both hosts issue the
certificate automatically once DNS resolves (allow up to an hour).

Rules: never commit or host game data (XBE, maps, halo_image.bin). Screenshots and clips only.
