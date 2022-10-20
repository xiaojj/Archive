import { ComponentEntry } from '@/components/types'
import { defineComponentMetadata } from '@/components/define'
import { videoChange } from '@/core/observer'
import { select } from '@/core/spin-query'
import { matchUrlPattern } from '@/core/utils'
import { useScopedConsole } from '@/core/utils/log'
import { mediaListUrls, videoAndBangumiUrls } from '@/core/utils/urls'

const getDanmakuCount = async () => {
  const countElement = await select(
    '.bilibili-player-video-info-danmaku-number, .bpx-player-video-info-dm',
  )
  /*
    3.x: 已装填 n 条弹幕
    2.x: n
  */
  const countText = countElement?.textContent ?? ''
  const countMatch = countText.match(/\d+/)
  if (!countMatch) {
    return null
  }
  const count = parseInt(countMatch[0])
  if (Number.isNaN(count)) {
    return null
  }
  return count
}

const entry: ComponentEntry = async ({ settings: { options } }) => {
  const console = useScopedConsole('展开弹幕列表')
  videoChange(async () => {
    if (mediaListUrls.some(url => matchUrlPattern(url)) && options.ignoreMediaList) {
      return
    }
    const danmakuCount = await getDanmakuCount()
    console.log(`当前弹幕量: ${danmakuCount}`)
    if (danmakuCount !== null && danmakuCount > options.maxDanmakuCount) {
      console.log(`超过了最大弹幕数量 ${options.maxDanmakuCount}, 跳过展开`)
      return
    }
    const danmakuBox = await select('.bui-collapse-wrap')
    if (danmakuBox && danmakuBox.classList.contains('bui-collapse-wrap-folded')) {
      const button = (await select('.bui-collapse-header')) as HTMLDivElement
      button?.click()
    }
  })
}

export const component = defineComponentMetadata({
  name: 'expandDanmakuList',
  displayName: '展开弹幕列表',
  entry,
  tags: [componentsTags.video],
  description: {
    'zh-CN':
      '每次加载视频时自动展开弹幕列表, 如果弹幕装载量超过 `最大弹幕数量`, 则不展开以免对页面造成卡顿.',
  },
  options: {
    ignoreMediaList: {
      defaultValue: true,
      displayName: '合集类页面不自动展开',
    },
    maxDanmakuCount: {
      defaultValue: 500,
      displayName: '最大弹幕数量',
    },
  },
  urlInclude: [...videoAndBangumiUrls, ...mediaListUrls],
})
