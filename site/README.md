# halovita.io — static site

Plain HTML/CSS, no build step. `index.html` + `style.css` + `img/`.

Deploy options:
- GitHub Pages: point Pages at the `site/` directory (or copy it to a `gh-pages` branch) and add a
  `CNAME` file containing `halovita.io`; at the registrar, CNAME `www` and ALIAS/A the apex to Pages.
- Cloudflare Pages / Netlify: publish directory `site`, no build command.

Rules: never commit or host game data (XBE, maps, halo_image.bin). Screenshots and clips only.
