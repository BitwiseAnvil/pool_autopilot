#!/usr/bin/env python3
"""Check the local viewer in Chromium and export the complete color wiring PDF.

Requires playwright; use --browser to select an installed Chromium executable.
All page loads are local files. Screenshots, if requested, go to --screenshots.
"""
import argparse
from pathlib import Path
from playwright.sync_api import sync_playwright
from wiring_data import SHARED_ENDS, load_wires

DOCS = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser', help='Path to an installed Chromium executable')
    parser.add_argument('--screenshots', type=Path, help='Optional directory for review PNGs')
    args = parser.parse_args()
    if args.screenshots:
        args.screenshots.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True, **({'executable_path': args.browser} if args.browser else {}))
        page = browser.new_page(viewport={'width': 1800, 'height': 1200}, device_scale_factor=1)
        errors = []
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.goto((DOCS/'physical-wiring.html').as_uri(), wait_until='load')
        assert page.locator('tr[data-wire]').count() == 27
        assert page.locator('.wire').count() == 27
        assert page.locator('img').evaluate_all('(imgs) => imgs.every(i => i.complete && i.naturalWidth > 0)')
        for sheet in ['overview', 'mains', 'neutral', 'switched', 'dc', 'returns', 'signals', 'ferrules']:
            page.locator(f'[data-show="{sheet}"]').click()
            assert page.locator(f'#sheet-{sheet}').is_visible()
            clipped = page.locator(f'#sheet-{sheet} svg').evaluate("""svg => [...svg.querySelectorAll('text')].filter(t => {
                const b=t.getBBox(), v=svg.viewBox.baseVal;
                return b.x < -1 || b.y < -1 || b.x+b.width > v.width+1 || b.y+b.height > v.height+1;
            }).map(t => t.textContent)""")
            assert not clipped, (sheet, clipped)
            if args.screenshots:
                page.locator(f'#sheet-{sheet} svg').screenshot(path=str(args.screenshots/f'{sheet}.png'))
        # Cross-sheet selection must highlight the whole conductor and both ends.
        for wire, sheet, endpoints in [('A01','mains',2), ('A08','neutral',2), ('B03','switched',2), ('C08','returns',2), ('C13','signals',2)]:
            page.locator('#wire').select_option(wire)
            assert page.locator(f'#sheet-{sheet}').is_visible()
            assert page.locator('.wire.selected').count() == 1
            assert page.locator('.endpoint.selected').count() == endpoints
            assert wire in page.locator('#detail').inner_text()
        # JS2 joins the factory GPIO1 lead plus scheduled R1 lead C12 and orange C13.
        page.locator('#clear').click()
        joint = page.locator('#sheet-signals [data-endpoint="JS2"]')
        joint.focus()
        page.keyboard.press('Enter')
        assert page.locator('#wire').input_value() == 'C12'
        page.keyboard.press('Enter')
        assert page.locator('#wire').input_value() == 'C13'
        # Each shared ferrule cycles every conductor, including three/four-wire ends.
        wire_rows={r['id']:r for r in load_wires()}
        for terminal,ids in SHARED_ENDS.items():
            sheet=wire_rows[ids[0]]['sheet']
            page.locator(f'[data-show="{sheet}"]').click()
            shared = page.locator(f'#sheet-{sheet} [data-endpoint="{terminal}"]')
            assert shared.get_attribute('data-wires') == ' '.join(ids)
            shared.focus()
            for wire in ids:
                page.keyboard.press('Enter')
                assert page.locator('#wire').input_value() == wire
                assert all(i in page.locator('#detail').inner_text() for i in ids)
        # All four neutral loads have single ends; only the power-terminal-3
        # branches show the user-reported FITTED status. A1/A2 never carry a shared ferrule.
        page.locator('[data-show="neutral"]').click()
        end=page.locator('#sheet-neutral [data-endpoint="K4:A2"]')
        assert end.get_attribute('data-wires')=='A16'
        assert end.get_attribute('data-prep')=='S14'
        end.focus()
        page.keyboard.press('Enter')
        assert page.locator('#wire').input_value()=='A16'
        for terminal in ('PS1:N','K5:A2','K1:A2'):
            assert page.locator(f'#sheet-neutral [data-endpoint="{terminal}"]').get_attribute('data-prep')=='S14'
        for terminal,status in [('K5:3','FITTED'),('K1:3','FITTED')]:
            shared=page.locator(f'#sheet-neutral [data-endpoint="{terminal}"]')
            assert shared.get_attribute('data-status')==status
            assert status in shared.text_content()
        assert page.locator('[data-strip],[data-bridge]').count()==0
        page.locator('#filter').fill('B03')
        assert page.locator('tr[data-wire]:visible').count() == 1
        page.locator('#zoom').click()
        assert page.locator('.map.zoomed').count() == 8
        # Printing must ignore selection, zoom and filters; never print an incomplete harness.
        page.emulate_media(media='print')
        assert page.locator('.drawing-sheet:visible').count() == 8
        assert page.locator('tr[data-wire]:visible').count() == 28
        assert page.locator('.wire.muted').first.evaluate('(n) => getComputedStyle(n).opacity') == '1'
        page.pdf(path=str(DOCS/'pooldose-physical-wiring.pdf'),print_background=True,
                 prefer_css_page_size=True,display_header_footer=False)
        page.emulate_media(media='screen')
        assert page.locator('tr[data-wire]:visible').count() == 1
        page.locator('#filter').fill('')
        page.locator('#zoom').click()
        page.locator('[data-show="overview"]').click()
        if args.screenshots:
            page.screenshot(path=str(args.screenshots/'viewer.png'),full_page=False)
        # Phone viewport must keep navigation reachable and scroll diagrams within their panels.
        page.set_viewport_size({'width':390,'height':844})
        assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
        page.locator('[data-show="signals"]').click()
        assert page.locator('#sheet-signals').is_visible()
        if args.screenshots:
            page.screenshot(path=str(args.screenshots/'mobile.png'),full_page=False)
        assert not errors, errors
        browser.close()
    print('Browser checks passed: 28 paths, both endpoints, sheet navigation, local GPIO splice selection, shared ferrules, single-blue neutral destinations, filtering, mobile and complete print output.')
    print(f'Exported {DOCS / "pooldose-physical-wiring.pdf"}')


if __name__ == '__main__':
    main()
