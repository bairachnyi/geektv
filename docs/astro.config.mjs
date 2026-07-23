import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Deployed from the bairachnyi/smalltv-ultra fork.
export default defineConfig({
  site: 'https://bairachnyi.github.io',
  base: '/smalltv-ultra',
  integrations: [
    starlight({
      title: 'SmallTV Ultra',
      description:
        'Custom firmware for GeekMagic SmallTV: GitHub operations dashboard, ticker, AI usage meter, web settings and OTA updates.',
      logo: {
        src: './src/assets/logo.svg',
        replacesTitle: false,
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/bairachnyi/smalltv-ultra',
        },
      ],
      editLink: {
        baseUrl: 'https://github.com/bairachnyi/smalltv-ultra/edit/main/docs/',
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
            { label: 'AI usage meter', link: '/features/usage/' },
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
