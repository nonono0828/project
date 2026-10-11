const fs = require('fs')
const path = require('path')
const sharp = require('sharp')

const frontendRoot = path.resolve(__dirname, '..')
const svgPath = path.join(frontendRoot, 'public', 'brand', 'zx-suture-mark.svg')
const resourceRoot = path.join(frontendRoot, 'android', 'app', 'src', 'main', 'res')
const svg = fs.readFileSync(svgPath)
const foregroundSvg = Buffer.from(
  svg
    .toString('utf8')
    .replace('<rect width="108" height="108" fill="#000"/>', ''),
)

const densities = {
  mdpi: 48,
  hdpi: 72,
  xhdpi: 96,
  xxhdpi: 144,
  xxxhdpi: 192,
}

async function renderIcon(size, output) {
  await sharp(svg, { density: 768 })
    .resize(size, size)
    .png()
    .toFile(output)
}

async function renderForeground(size, output) {
  await sharp(foregroundSvg, { density: 768 })
    .resize(size, size)
    .png()
    .toFile(output)
}

async function renderRoundIcon(size, output) {
  const mask = Buffer.from(
    `<svg width="${size}" height="${size}"><circle cx="${size / 2}" cy="${size / 2}" r="${size / 2}" fill="#fff"/></svg>`,
  )
  await sharp(svg, { density: 768 })
    .resize(size, size)
    .composite([{ input: mask, blend: 'dest-in' }])
    .png()
    .toFile(output)
}

async function renderSplash(inputPath) {
  const { width, height } = await sharp(inputPath).metadata()
  const short = Math.min(width, height)
  const iconSize = Math.round(short * 0.3)
  const icon = await sharp(svg, { density: 768 })
    .resize(iconSize, iconSize)
    .png()
    .toBuffer()

  await sharp({
    create: {
      width,
      height,
      channels: 4,
      background: '#000000',
    },
  })
    .composite([{
      input: icon,
      left: Math.round((width - iconSize) / 2),
      top: Math.round((height - iconSize) / 2),
    }])
    .png()
    .toFile(`${inputPath}.new.png`)

  fs.renameSync(`${inputPath}.new.png`, inputPath)
}

async function walkSplashFiles(dir) {
  const files = []
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const fullPath = path.join(dir, entry.name)
    if (entry.isDirectory()) files.push(...await walkSplashFiles(fullPath))
    else if (entry.name === 'splash.png') files.push(fullPath)
  }
  return files
}

async function main() {
  for (const [density, size] of Object.entries(densities)) {
    const folder = path.join(resourceRoot, `mipmap-${density}`)
    fs.mkdirSync(folder, { recursive: true })
    await renderIcon(size, path.join(folder, 'ic_launcher.png'))
    await renderRoundIcon(size, path.join(folder, 'ic_launcher_round.png'))
    await renderIcon(size, path.join(folder, 'ic_launcher_foreground.png'))
    await renderIcon(size, path.join(folder, 'ic_launcher_zx.png'))
    await renderRoundIcon(size, path.join(folder, 'ic_launcher_zx_round.png'))
    await renderForeground(size, path.join(folder, 'ic_launcher_zx_foreground.png'))
  }

  for (const splashPath of await walkSplashFiles(resourceRoot)) {
    await renderSplash(splashPath)
  }

  const previewTargets = [
    path.join(frontendRoot, 'public', 'app-icon-512.png'),
    path.join(frontendRoot, 'design', 'zx-suture-app-icon-512.png'),
  ]
  for (const target of previewTargets) {
    fs.mkdirSync(path.dirname(target), { recursive: true })
    await renderIcon(512, target)
  }

  console.log('ZX branding generated for web and Android.')
}

main().catch((error) => {
  console.error(error)
  process.exitCode = 1
})
