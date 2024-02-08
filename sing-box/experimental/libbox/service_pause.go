package libbox

import (
	"sync"
	"time"
)

type servicePauseFields struct {
	pauseAccess sync.Mutex
	pauseTimer  *time.Timer
}

func (s *BoxService) Pause() {
	s.pauseAccess.Lock()
	defer s.pauseAccess.Unlock()

	if s.pauseTimer != nil {
		s.pauseTimer.Stop()
	}

	s.pauseTimer = time.AfterFunc(time.Minute, s.pause)
}

func (s *BoxService) pause() {
	s.pauseManager.DevicePause()
	_ = s.instance.Router().ResetNetwork()
}

func (s *BoxService) Wake() {
	s.pauseAccess.Lock()
	defer s.pauseAccess.Unlock()

	if s.pauseTimer != nil {
		s.pauseTimer.Stop()
		s.pauseTimer = nil
	}

	s.pauseManager.DeviceWake()
	_ = s.instance.Router().ResetNetwork()
}
