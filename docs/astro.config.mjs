import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Optional local/static documentation build.
export default defineConfig({
  site: 'https://bairachnyi.github.io',
  base: '/geektv',
  integrations: [
    starlight({
      title: 'GeekTV',
      description:
        'Open firmware for GeekMagic SmallTV: clocks, weather, gallery, Codex, GitHub operations, ticker, web settings and OTA.',
      logo: {
        src: './src/assets/logo.svg',
        replacesTitle: false,
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/bairachnyi/geektv',
        },
      ],
      editLink: {
        baseUrl: 'https://github.com/bairachnyi/geektv/edit/main/docs/',
      },
      sidebar: [
        { label: 'Home', link: '/' },
        {
          label: 'Getting started',
          items: [
            { label: 'Hardware and variants', link: '/getting-started/hardware/' },
            { label: 'Flashing', link: '/getting-started/flashing/' },
            { label: 'First-time setup', link: '/getting-started/setup/' },
          ],
        },
        {
          label: 'Features',
          items: [
            { label: 'Stock and crypto ticker', link: '/features/ticker/' },
            { label: 'Clock and weather', link: '/features/clock-weather/' },
            { label: 'Gallery', link: '/features/gallery/' },
            { label: 'Codex usage', link: '/features/codex/' },
            { label: 'GitHub GH//STAT', link: '/features/github/' },
          ],
        },
        {
          label: 'Reference',
          items: [
            { label: 'Data sources', link: '/reference/data-sources/' },
            { label: 'All settings', link: '/reference/settings/' },
            { label: 'Firmware architecture', link: '/reference/architecture/' },
            { label: 'HTTP API', link: '/reference/http-api/' },
            { label: 'Building from source', link: '/reference/building/' },
            { label: 'Recovery and credits', link: '/reference/recovery/' },
          ],
        },
      ],
    }),
  ],
});
