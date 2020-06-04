;;;
;;; srfi-189 - Maybe and Either
;;;
;;;   Copyright (c) 2020  Shiro Kawai  <shiro@acm.org>
;;;
;;;   Redistribution and use in source and binary forms, with or without
;;;   modification, are permitted provided that the following conditions
;;;   are met:
;;;
;;;   1. Redistributions of source code must retain the above copyright
;;;      notice, this list of conditions and the following disclaimer.
;;;
;;;   2. Redistributions in binary form must reproduce the above copyright
;;;      notice, this list of conditions and the following disclaimer in the
;;;      documentation and/or other materials provided with the distribution.
;;;
;;;   3. Neither the name of the authors nor the names of its contributors
;;;      may be used to endorse or promote products derived from this
;;;      software without specific prior written permission.
;;;
;;;   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;;;   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;;;   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;;;   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;;;   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;;;   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
;;;   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
;;;   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
;;;   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
;;;   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
;;;   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;

(define-module srfi-189
  (use srfi-1)
  (use util.match)
  (export just nothing right left either-swap
          maybe? either? just? nothing? right? left?
          maybe= either=
          maybe-ref either-ref maybe-ref/default either-ref/default
          maybe-join either-join
          ;; maybe-compose either-compose
          ;; maybe-bind either-bind
          ;; maybe-length either-length
          ;; maybe-filter maybe-remove either-filter either-remove
          ;; maybe-sequence either-sequence
          ;; either->maybe list->just list->right
          ;; maybe->list either->list
          ;; maybe->lisp lisp->maybe
          ;; maybe->eof eof->maybe
          ;; maybe->values either->values
          ;; values->maybe values->either
          ;; maybe->lisp-values either->lisp-values
          ;; lisp-values->maybe lisp-values->either
          ;; maybe-map either-map maybe-for-each either-for-each
          ;; maybe-fold either-fold maybe-unfold either-unfold
          ;; maybe-if

          ;; try-not try=? tri-and tri-or tri-merge
          )
  )
(select-module srfi-189)

(define-class <maybe> () ())

(define-class <just> (<maybe>)
  ((objs :init-keyword :objs)))
(define-class <nothing> (<maybe>) ())

(define-class <either> () ())

(define-class <right> (<either>)
  ((objs :init-keyword :objs)))
(define-class <left> (<either>)
  ((objs :init-keyword :objs)))

(define *nothing* (make <nothing>))

;; API
(define (just . objs) (make <just> :objs objs))
(define (nothing) *nothing*)
(define (right . objs) (make <right> :objs objs))
(define (left . objs) (make <left> :objs objs))

(define (either-swap either)
  (assume-type either <either>)
  (make (if (left? either) <right> <left>) :objs (~ either'objs)))

(define (maybe? x) (is-a? x <maybe>))
(define (just? x) (is-a? x <just>))
(define (nothing? x) (is-a? x <nothing>))
(define (either? x) (is-a? x <either>))
(define (right? x) (is-a? x <right>))
(define (left? x) (is-a? x <left>))

(define (maybe= eqproc x y)
  (or (and (nothing? x) (nothing? y))
      (and (just? x) (just? y)
           (list= eqproc (~ x'objs) (~ y'objs)))))
(define (either= eqproc x y)
  (or (and (right? x) (right? y)
           (list= eqproc (~ x'objs) (~ y'objs)))
      (and (just? x) (just? y)
           (list= eqproc (~ x'objs) (~ y'objs)))))

(define (%maybe-ref-failure)
  (error "Attempt to derefenence <nothing>"))
(define (%either-ref-failure . args)
  (error "Attempt to derefenence <left> with values" args))

(define (maybe-ref maybe :optional (failure %maybe-ref-failure) 
                                   (success values))
  (assume-type maybe <maybe>)
  (if (nothing? maybe)
    (failure)
    (apply success (~ maybe'objs))))

(define (either-ref either :optional (failure %either-ref-failure) 
                                     (success values))
  (assume-type either <either>)
  (if (left? either)
    (apply failure (~ either'objs))
    (apply success (~ either'objs))))

(define (maybe-ref/default maybe . defaults)
  (assume-type maybe <maybe>)
  (apply values (if (just? maybe) (~ maybe'objs) defaults)))

(define (either-ref/default either . defaults)
  (assume-type either <either>)
  (apply values (if (right? either) (~ either'objs) defaults)))

(define (maybe-join maybe)
  (assume-type maybe <maybe>)
  (if (nothing? maybe)
    maybe
    (match (~ maybe'objs)
      [((? maybe? val)) val]
      [x (error "invalid payload" x)])))

(define (either-join either)
  (assume-type either <either>)
  (if (left? either)
    either
    (match (~ either'objs)
      [((? right? val)) val]
      [x (error "invalid payload" x)])))
      
  


  
          
