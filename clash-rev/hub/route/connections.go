package route

import (
	"bytes"
	"encoding/json"
	"net/http"
	"strconv"
	"time"

	"github.com/MerlinKodo/clash-rev/tunnel/statistic"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/render"
	"github.com/gobwas/ws"
	"github.com/gobwas/ws/wsutil"
)

func connectionRouter() http.Handler {
	r := chi.NewRouter()
	r.Get("/", getConnections)
	r.Delete("/", closeAllConnections)
	r.Delete("/{id}", closeConnection)
	r.Post("/close", closeMultiConnections)
	return r
}

func getConnections(w http.ResponseWriter, r *http.Request) {
	if !(r.Header.Get("Upgrade") == "websocket") {
		snapshot := statistic.DefaultManager.Snapshot()
		render.JSON(w, r, snapshot)
		return
	}

	conn, _, _, err := ws.UpgradeHTTP(r, w)
	if err != nil {
		return
	}

	intervalStr := r.URL.Query().Get("interval")
	interval := 1000
	if intervalStr != "" {
		t, err := strconv.Atoi(intervalStr)
		if err != nil {
			render.Status(r, http.StatusBadRequest)
			render.JSON(w, r, ErrBadRequest)
			return
		}

		interval = t
	}

	buf := &bytes.Buffer{}
	sendSnapshot := func() error {
		buf.Reset()
		snapshot := statistic.DefaultManager.Snapshot()
		if err := json.NewEncoder(buf).Encode(snapshot); err != nil {
			return err
		}

		return wsutil.WriteMessage(conn, ws.StateServerSide, ws.OpText, buf.Bytes())
	}

	if err := sendSnapshot(); err != nil {
		return
	}

	tick := time.NewTicker(time.Millisecond * time.Duration(interval))
	defer tick.Stop()
	for range tick.C {
		if err := sendSnapshot(); err != nil {
			break
		}
	}
}

func closeConnection(w http.ResponseWriter, r *http.Request) {
	closeSingleConnection(chi.URLParam(r, "id"))
	render.NoContent(w, r)
}

func closeAllConnections(w http.ResponseWriter, r *http.Request) {
	statistic.DefaultManager.Range(func(c statistic.Tracker) bool {
		_ = c.Close()
		return true
	})
	render.NoContent(w, r)
}

type idList struct {
	Ids []string `json:"ids"`
}

func closeMultiConnections(w http.ResponseWriter, r *http.Request) {
	var list idList
	if err := json.NewDecoder(r.Body).Decode(&list); err != nil {
		render.Status(r, http.StatusBadRequest)
		render.JSON(w, r, ErrBadRequest)
		return
	}
	for _, id := range list.Ids {
		closeSingleConnection(id)
	}
	render.NoContent(w, r)
}

func closeSingleConnection(id string) {
	if c := statistic.DefaultManager.Get(id); c != nil {
		_ = c.Close()
	}
}
