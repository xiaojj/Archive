/** 表示一条评论回复 */
export class CommentReplyItem extends EventTarget {
  /** 对应元素 */
  element: HTMLElement
  /** 评论ID */
  id: string
  /** 评论者UID */
  userId: string
  /** 用户名 */
  userName: string
  /** 评论内容 */
  content: string
  /** (仅 v1) 评论时间 (文本) */
  timeText?: string
  /** (仅 v2) 评论时间戳 */
  time?: number
  /** 点赞数 */
  likes: number

  constructor(initParams: Omit<CommentReplyItem, keyof EventTarget>) {
    super()
    this.element = initParams.element
    this.id = initParams.id
    this.userId = initParams.userId
    this.userName = initParams.userName
    this.content = initParams.content
    this.timeText = initParams.timeText
    this.time = initParams.time
    this.likes = initParams.likes
  }
}
